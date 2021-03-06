#include <stdio.h>
#include <stdlib.h>
#include "Signal.h"

#ifdef _WIN32
#include <windows.h>
#include "OlsApi.h"
#endif

#ifdef __linux
#include "cpuPrimitives.h"
#include <string.h>
#endif

#include "Processor.h"
#include "Llano.h"
#include "PCIRegObject.h"
#include "MSRObject.h"
#include "PerformanceCounter.h"

//Llano class constructor
Llano::Llano() {
	DWORD eax, ebx, ecx, edx;
	DWORD nodes;
	DWORD cores;

	didDivisors[0] = 1;
	didDivisors[1] = 1.5;
	didDivisors[2] = 2;
	didDivisors[3] = 3;
	didDivisors[4] = 4;
	didDivisors[5] = 6;
	didDivisors[6] = 8;
	didDivisors[7] = 12;
	didDivisors[8] = 16;

	//Check extended CpuID Information - CPUID Function 0000_0001 reg EAX
	if (Cpuid(0x1, &eax, &ebx, &ecx, &edx) != TRUE) {
		printf(
				"Llano::Llano - Fatal error during querying for Cpuid(0x1) instruction.\n");
		return;
	}

	int familyBase = (eax & 0xf00) >> 8;
	int model = (eax & 0xf0) >> 4;
	int stepping = eax & 0xf;
	int familyExtended = ((eax & 0xff00000) >> 20) + familyBase;
	int modelExtended = ((eax & 0xf0000) >> 12) + model; /* family 12h: modelExtended is valid */

	//Check Brand ID and Package type - CPUID Function 8000_0001 reg EBX
	if (Cpuid(0x80000001, &eax, &ebx, &ecx, &edx) != TRUE) {
		printf(
				"Llano::Llano - Fatal error during querying for Cpuid(0x80000001) instruction.\n");
		return;
	}

	int brandId = (ebx & 0xffff);
	int processorModel = (brandId >> 4) & 0x7f;
	int string1 = (brandId >> 11) & 0xf;
	int string2 = (brandId & 0xf);
	int pkgType = (ebx >> 28);

	//Sets processor Specs
	setSpecFamilyBase(familyBase);
	setSpecModel(model);
	setSpecStepping(stepping);
	setSpecFamilyExtended(familyExtended);
	setSpecModelExtended(modelExtended);
	setSpecBrandId(brandId);
	setSpecProcessorModel(processorModel);
	setSpecString1(string1);
	setSpecString2(string2);
	setSpecPkgType(pkgType);
	setBoostStates(0);
	setMaxSlots(4);

	//Llano platform will always contain one node per system, since it has no Hypertransport Link.
	//Maybe can change in the future.
	nodes = 1;

	//Check how many physical cores are present - CPUID Function 8000_0008 reg ECX
	if (Cpuid(0x80000008, &eax, &ebx, &ecx, &edx) != TRUE) {
		printf(
				"Llano::Llano- Fatal error during querying for Cpuid(0x80000008) instruction.\n");
		return;
	}

	cores = (ecx & 0xff) + 1; /* cores per package */

	setProcessorNodes(nodes);
	setProcessorCores(cores);
	setPowerStates(8);
	setProcessorIdentifier(PROCESSOR_12H_FAMILY);
	setProcessorStrId("Family 12h Llano Processor");

}

/*
 * Static methods to allow external Main to detect current configuration status
 * without instantiating an object. This method that detects if the system
 * has a processor supported by this module
 */
bool Llano::isProcessorSupported() {
	DWORD eax;
	DWORD ebx;
	DWORD ecx;
	DWORD edx;

	//TODO: remove to avoid simulation
	//return true;

	//Check base CpuID information
	if (Cpuid(0x0, &eax, &ebx, &ecx, &edx) != TRUE)
		return false;

	//Checks if eax is 0x6. It determines the largest CPUID function available
	//Family 12h returns eax=0x6
	if (eax != 0x6)
		return false;

	//Check "AuthenticAMD" string
	if ((ebx != 0x68747541) || (ecx != 0x444D4163) || (edx != 0x69746E65))
		return false;

	//Check extended CpuID Information - CPUID Function 0000_0001 reg EAX
	if (Cpuid(0x1, &eax, &ebx, &ecx, &edx) != TRUE)
		return false;

	int familyBase = (eax & 0xf00) >> 8;
	int familyExtended = ((eax & 0xff00000) >> 20) + familyBase;

	if (familyExtended != 0x12)
		return false;

	//Detects a Family 12h processor, i.e. Llano processor (A-series or E2-series)
	return true;
}

void Llano::showFamilySpecs() {

	printf("Not yet implemented.\n");

}

//Miscellaneous function inherited by Processor abstract class and that
//needs to be reworked for family 10h
float Llano::convertVIDtoVcore(DWORD curVid) {

	/*How to calculate VID from Vcore.

	 Serial VID Interface is simple to calculate. Family 12h uses only Serial VID
	 To obtain vcore from VID you need to do:

	 vcore = 1,55 – (VID * 0.0125)

	 The inverse formula to obtain VID from vcore is:

	 vid = (1.55-vcore)/0.0125

	 */

	float curVcore;

	if (curVid >= 0x7c)
		curVcore = 0;
	else
		curVcore = (float) (1.550 - (0.0125 * curVid));

	return curVcore;
}

DWORD Llano::convertVcoretoVID(float vcore) {
	DWORD vid;

	vid = round(((1.55 - vcore) / 0.0125));

	return vid;

}

float Llano::roundDivisor(float divisor) {

	int i;

	for (i = 0; i < 9; i++)
		if (divisor <= didDivisors[i])
			return didDivisors[i];

	return didDivisors[7];

}

int Llano::roundDivisorToDid(float divisor) {

	int i;

	for (i = 0; i < 9; i++)
		if (divisor <= didDivisors[i])
			return i;

	return 7;

}

DWORD Llano::convertFDtoFreq(float fid, float divisor) {

	return (int) ((100 * ((int) fid + 0x10)) / divisor);

}

void Llano::convertFreqtoFD(DWORD freq, float *oFid, float *oDivisor) {

	/*
	 *
	 For family 12h processor the right formula is:
	 (cfr. BKDG for AMD Family 12h, doc #41131 rev 3.00 - page 468)

	 (100 * (fid + 16)) / (divisor specified by DID)

	 Inverse formula is

	 fid= ((divisor * freq) / 100) - 16

	 did = roundDivisorToDid ((100 * (fid +16))/f)

	 */

	float fid;
	int did;

	if (freq == 0)
		return;

	did = 0;
	do {

		fid = (((didDivisors[did]) * (float) freq) / 100) - 16;

		if (fid < 0)
			did++;

	} while (fid < 0);

	if (fid > 31)
		fid = 31;

	//Actually we don't need to reculate DID, since we guessed a
	//valid one due to the fact that the argument is positive.

	*oFid = round(fid);
	*oDivisor = didDivisors[did];

	//TODO: Debug printf:
	printf("\n\nFor frequency %d, FID is %f, DID %f\n", freq, *oFid, *oDivisor);

	return;

}

//-----------------------setVID-----------------------------
//Overloads abstract class setVID to allow per-core personalization
void Llano::setVID(PState ps, DWORD vid) {

	MSRObject *msrObject;

	if ((vid > minVID()) || (vid < maxVID())) {
		printf("Llano.cpp: VID Allowed range %d-%d\n", minVID(), maxVID());
		return;
	}

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp: unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To set VID, base offset is 9 bits and value is 7 bit wide.

	msrObject->setBitsLow(9, 7, vid);

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp: unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

//-----------------------setFID-----------------------------
//Overloads abstract Processor method to allow per-core personalization
void Llano::setFID(PState ps, float floatFid) {

	unsigned int fid;

	MSRObject *msrObject;

	fid = (unsigned int) round(floatFid);

	if (fid > 31) {
		printf("Llano.cpp: FID Allowed range 0-31\n");
		return;
	}

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp: unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To set FID, base offset is 4 bits and value is 5 bit wide
	msrObject->setBitsLow(4, 5, fid);

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp: unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

//-----------------------setDID-----------------------------
//Overloads abstract Processor method to allow per-core personalization
void Llano::setDID(PState ps, float divisor) {

	unsigned int did;
	MSRObject *msrObject;

	did = roundDivisorToDid(divisor);

	if (divisor < 1 || divisor > 16) {
		printf("Llano.cpp: divisor(DID) allowed range 1-16\n");
		return;
	}

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp: unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To set DID, base offset is 0 bits and value is 4 bit wide
	msrObject->setBitsLow(0, 4, did);

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp: unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}
//-----------------------getVID-----------------------------
DWORD Llano::getVID(PState ps) {

	MSRObject *msrObject;
	DWORD vid;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::getVID - unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask.
	//VID is stored after 9 bits of offset and is 7 bits wide
	vid = msrObject->getBitsLow(0, 9, 7);

	free(msrObject);

	return vid;

}

//-----------------------getFID-----------------------------

float Llano::getFID(PState ps) {

	MSRObject *msrObject;
	DWORD fid;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::getFID - unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask (cpu 0)
	//FID is stored after 4 bits of offset and is 5 bits wide
	fid = msrObject->getBitsLow(0, 4, 5);

	free(msrObject);

	return fid;

}

//-----------------------getDID-----------------------------

float Llano::getDID(PState ps) {

	MSRObject *msrObject;
	float divisor;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::getDID - unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask (cpu 0)
	//DID is stored after 0 bits of offset and is 4 bits wide

	divisor = didDivisors[msrObject->getBitsLow(0, 0, 4)];

	free(msrObject);

	return divisor;
}

//-----------------------setFrequency-----------------------------

void Llano::setFrequency(PState ps, DWORD freq) {

	float did;
	float fid;

	convertFreqtoFD(freq, &fid, &did);

	setFID(ps, fid);
	setDID(ps, did);

	return;
}

//-----------------------setVCore-----------------------------

void Llano::setVCore(PState ps, float vcore) {
	DWORD vid;

	vid = convertVcoretoVID(vcore);

	//Check if VID is below maxVID value set by the processor.
	//If it is, then there are no chances the processor will accept it and
	//we reply with an error
	if (vid < maxVID()) {
		printf(
				"Unable to set vcore: %0.4fV exceeds maximum allowed vcore (%0.4fV)\n",
				vcore, convertVIDtoVcore(maxVID()));
		return;
	}

	//Again we che if VID is above minVID value set by processor.
	if (vid > minVID()) {
		printf(
				"Unable to set vcore: %0.4fV is below minimum allowed vcore (%0.4fV)\n",
				vcore, convertVIDtoVcore(minVID()));
		return;
	}

	setVID(ps, vid);

	return;

}

//-----------------------getFrequency-----------------------------
DWORD Llano::getFrequency(PState ps) {

	float curDid;
	float curFid;
	DWORD curFreq;

	curFid = getFID(ps);
	curDid = getDID(ps);

	curFreq = convertFDtoFreq(curFid, curDid);

	return curFreq;

}

//-----------------------getVCore-----------------------------

float Llano::getVCore(PState ps) {
	DWORD curVid;
	float curVcore;

	curVid = getVID(ps);

	curVcore = convertVIDtoVcore(curVid);

	return curVcore;
}

//PStates enable/disable/peek
void Llano::pStateDisable(PState ps) {

	MSRObject *msrObject;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::pStateDisable - unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To disable a pstate, base offset is 63 bits (31th bit of edx) and value is 1 bit wide
	msrObject->setBitsHigh(31, 1, 0x0);

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp::pStateDisable - unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

void Llano::pStateEnable(PState ps) {

	MSRObject *msrObject;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::pStateEnable - unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To disable a pstate, base offset is 63 bits (31th bit of edx) and value is 1 bit wide
	msrObject->setBitsHigh(31, 1, 0x1);

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp:pStateEnable - unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

bool Llano::pStateEnabled(PState ps) {

	MSRObject *msrObject;
	unsigned int status;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_12H_PSTATEMSR + ps.getPState(), getMask())) {
		printf("Llano.cpp::pStateEnabled - unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//To peek a pstate, base offset is 63 bits (31th bit of edx) and value is 1 bit wide
	//We consider just the first cpu in cpuMask
	status = msrObject->getBitsHigh(0, 31, 1);

	free(msrObject);

	if (status == 0)
		return false;
	else
		return true;

}

void Llano::setMaximumPState(PState ps) {

	PCIRegObject *pciRegObject;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xdc, getNodeMask())) {
		printf("Llano.cpp::setMaximumPState - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * Maximum pstate is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xdc
	 * bits from 8 to 10
	 */
	pciRegObject->setBits(8, 3, ps.getPState());

	if (!pciRegObject->writePCIReg()) {
		printf("Llano.cpp::setMaximumPState - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

PState Llano::getMaximumPState() {

	PCIRegObject *pciRegObject;
	PState pState(0);

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xdc, getNodeMask())) {
		printf("Llano.cpp::getMaximumPState - unable to read PCI register\n");
		free(pciRegObject);
		return 0;
	}

	/*
	 * Maximum pstate is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xdc
	 * bits from 8 to 10
	 */
	pState.setPState(pciRegObject->getBits(0, 8, 3));

	free(pciRegObject);

	return pState;

}

void Llano::forcePState(PState ps) {

	MSRObject *msrObject;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(BASE_PSTATE_CTRL_REG, getMask())) {
		printf("Llano.cpp::forcePState - unable to read MSR\n");
		free(msrObject);
		return;
	}

	//To force a pstate, we act on setting the first 3 bits of register. All other bits must be zero
	msrObject->setBitsLow(0, 32, 0x0);
	msrObject->setBitsHigh(0, 32, 0x0);
	msrObject->setBitsLow(0, 3, ps.getPState());

	if (!msrObject->writeMSR()) {
		printf("Llano.cpp::forcePState - unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

//minVID is reported per-node, so selected core is always discarded
DWORD Llano::minVID() {

	MSRObject *msrObject;
	DWORD minVid;

	msrObject = new MSRObject;

	if (!msrObject->readMSR(COFVID_STATUS_REG, getMask(0, selectedNode))) {
		printf("Llano::minVID - Unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//minVid is stored in COFVID_STATUS_REG in high half register (edx)
	//from bit 10 to bit 16
	minVid = msrObject->getBitsHigh(0, 10, 7);

	free(msrObject);

	//If minVid==0, then there's no minimum vid.
	//Since the register is 7-bit wide, then 127 is
	//the maximum value allowed.
	if (getPVIMode()) {
		//Parallel VID mode, allows minimum vcore VID up to 0x5d
		if (minVid == 0)
			return 0x5d;
		else
			return minVid;
	} else {
		//Serial VID mode, allows minimum vcore VID up to 0x7b
		if (minVid == 0)
			return 0x7b;
		else
			return minVid;
	}
}

//maxVID is reported per-node, so selected core is always discarded
DWORD Llano::maxVID() {
	MSRObject *msrObject;
	DWORD maxVid;

	msrObject = new MSRObject;

	if (!msrObject->readMSR(COFVID_STATUS_REG, getMask(0, selectedNode))) {
		printf("Llano::maxVID - Unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//maxVid is stored in COFVID_STATUS_REG in high half register (edx)
	//from bit 3 to bit 9
	maxVid = msrObject->getBitsHigh(0, 3, 7);

	free(msrObject);

	//If maxVid==0, then there's no maximum set in hardware
	if (maxVid == 0)
		return 0;
	else
		return maxVid;
}

//StartupPState is reported per-node. Selected core is discarded
DWORD Llano::startupPState() {
	MSRObject *msrObject;
	DWORD pstate;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(COFVID_STATUS_REG, getMask(0, selectedNode))) {
		printf("Llano.cpp::startupPState unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask (cpu 0)
	//StartupPState has base offset at 0 bits of high register (edx) and is 3 bit wide
	pstate = msrObject->getBitsHigh(0, 0, 3);

	free(msrObject);

	return pstate;

}

DWORD Llano::maxCPUFrequency() {

	//return (0+0x10)*100; //Returns 1600 Mhz processor --- simulated stub!!!

	MSRObject *msrObject;
	DWORD maxCPUFid;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(COFVID_STATUS_REG, getMask(0, selectedNode))) {
		printf("Llano.cpp::maxCPUFrequency unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask (cpu 0)
	//maxCPUFid has base offset at 17 bits of high register (edx) and is 6 bits wide
	maxCPUFid = msrObject->getBitsHigh(0, 17, 6);

	free(msrObject);

	return (maxCPUFid + 0x10) * 100;

}

//Temperature registers ------------------
DWORD Llano::getTctlRegister(void) {

	PCIRegObject *pciRegObject;
	DWORD temp;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa4, getNodeMask())) {
		printf("Llano.cpp::getTctlRegister - unable to read PCI register\n");
		free(pciRegObject);
		return 0;
	}

	/*
	 * Tctl data is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa4
	 * bits from 21 to 31
	 */
	temp = pciRegObject->getBits(0, 21, 11);

	free(pciRegObject);

	return temp >> 3;
}

DWORD Llano::getTctlMaxDiff() {

	PCIRegObject *pciRegObject;
	DWORD maxDiff;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa4, getNodeMask())) {
		printf("Llano.cpp::getTctlMaxDiff unable to read PCI register\n");
		free(pciRegObject);
		return 0;
	}

	/*
	 * Tctl Max diff data is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa4
	 * bits from 5 to 6
	 */
	maxDiff = pciRegObject->getBits(0, 5, 2);

	free(pciRegObject);

	return maxDiff;

}

//Voltage Slamming time
DWORD Llano::getRampTime(void) {
	PCIRegObject *pciRegObject;
	DWORD slamTime;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xd8, getNodeMask())) {
		printf("Llano.cpp::getRampTime unable to read PCI register\n");
		free(pciRegObject);
		return 0;
	}

	/*
	 * voltage ramptime is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xd8
	 * bits from 4 to 6
	 */
	slamTime = pciRegObject->getBits(0, 4, 3);

	free(pciRegObject);

	return slamTime;
}

void Llano::setRampTime(DWORD slmTime) {

	PCIRegObject *pciRegObject;

	if (slmTime < 0 || slmTime > 7) {
		printf("Invalid Ramp Time: must be between 0 and 7\n");
		return;
	}

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xd8, getNodeMask())) {
		printf("Llano::setRampTime -  unable to read PCI Register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * voltage slamtime is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xd8
	 * bits from 4 to 6
	 */

	pciRegObject->setBits(4, 3, slmTime);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano.cpp::setRampTime - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

// AltVID - HTC Thermal features

bool Llano::HTCisCapable() {
	PCIRegObject *pciRegObject;
	DWORD isCapable;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xe8, getNodeMask())) {
		printf("Llano::HTCisCapable - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC-capable bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xe8
	 * bit 10
	 */

	isCapable = pciRegObject->getBits(0, 10, 1);

	free(pciRegObject);

	return (bool) isCapable;
}

bool Llano::HTCisEnabled() {
	PCIRegObject *pciRegObject;
	DWORD isEnabled;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCisEnabled - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC-enabled bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 0
	 */

	isEnabled = pciRegObject->getBits(0, 0, 1);

	free(pciRegObject);

	return (bool) isEnabled;

}

bool Llano::HTCisActive() {
	PCIRegObject *pciRegObject;
	DWORD isActive;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCisActive - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC active bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 4
	 */

	isActive = pciRegObject->getBits(0, 4, 1);

	free(pciRegObject);

	return (bool) isActive;

}

bool Llano::HTChasBeenActive() {
	PCIRegObject *pciRegObject;
	DWORD hasBeenActivated;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTChasBeenActive - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC has been activated bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 5
	 */

	hasBeenActivated = pciRegObject->getBits(0, 5, 1);

	free(pciRegObject);

	return (bool) hasBeenActivated;

}

DWORD Llano::HTCTempLimit() {
	PCIRegObject *pciRegObject;
	DWORD tempLimit;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCTempLimit - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC temperature limit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bits from 16 to 22
	 */

	tempLimit = 52 + (pciRegObject->getBits(0, 16, 7) >> 1);

	free(pciRegObject);

	return tempLimit;

}

bool Llano::HTCSlewControl() {
	PCIRegObject *pciRegObject;
	DWORD slewControl;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCSlewControl - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC slew Control bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 23
	 */

	slewControl = pciRegObject->getBits(0, 23, 1);

	free(pciRegObject);

	return (bool) slewControl;

}

DWORD Llano::HTCHystTemp() {
	PCIRegObject *pciRegObject;
	DWORD hystTemp;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCHystTemp - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC Hysteresis Temperature is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bits from 24 to 27
	 */

	hystTemp = pciRegObject->getBits(0, 24, 4) >> 1;

	free(pciRegObject);

	return hystTemp;

}

DWORD Llano::HTCPStateLimit() {

	PCIRegObject *pciRegObject;
	DWORD pStateLimit;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCPStateLimit - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC PState limit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bits from 28 to 30
	 */

	pStateLimit = pciRegObject->getBits(0, 28, 3);

	free(pciRegObject);

	return pStateLimit;

}

bool Llano::HTCLocked() {
	PCIRegObject *pciRegObject;
	DWORD htcLocked;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCLocked - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * HTC locked bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 31
	 */

	htcLocked = pciRegObject->getBits(0, 31, 1);

	free(pciRegObject);

	return (bool) htcLocked;
}

void Llano::HTCEnable() {
	PCIRegObject *pciRegObject;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCEnable - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * HTC-enabled bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 0
	 */

	pciRegObject->setBits(0, 1, 1);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano::HTCEnable - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;
}

void Llano::HTCDisable() {
	PCIRegObject *pciRegObject;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCDisable - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * HTC-enabled bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bit 0
	 */

	pciRegObject->setBits(0, 1, 0);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano::HTCDisable - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

void Llano::HTCsetTempLimit(DWORD tempLimit) {

	PCIRegObject *pciRegObject;

	if (tempLimit < 52 || tempLimit > 115) {
		printf("HTCsetTempLimit: accepted range between 52 and 115\n");
		return;
	}

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCsetTempLimit - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * HTC temp limit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bits from 16 to 22
	 */

	pciRegObject->setBits(16, 7, (tempLimit - 52) << 1);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano::HTCsetTempLimit - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

void Llano::HTCsetHystLimit(DWORD hystLimit) {

	PCIRegObject *pciRegObject;

	if (hystLimit < 0 || hystLimit > 7) {
		printf("HTCsetHystLimit: accepted range between 0 and 7\n");
		return;
	}

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0x64, getNodeMask())) {
		printf("Llano::HTCsetHystLimit - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * HTC temp limit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0x64
	 * bits from 24 to 27
	 */

	pciRegObject->setBits(24, 4, hystLimit << 1);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano::HTCsetHystLimit - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;
}


/* todo: needs to be revised, if delivers coherent values or not */
DWORD Llano::getAltVID() {

	PCIRegObject *pciRegObject;
	DWORD altVid;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xdc, getNodeMask())) {
		printf("Llano.cpp::getAltVID - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * AltVid is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xDC
	 * bits from 0 to 6
	 */

	altVid = pciRegObject->getBits(0, 0, 7);

	free(pciRegObject);

	return altVid;
}

void Llano::setAltVid(DWORD altVid) {

	PCIRegObject *pciRegObject;

	if ((altVid < maxVID()) || (altVid > minVID())) {
		printf("setAltVID: VID Allowed range %d-%d\n", maxVID(), minVID());
		return;
	}

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xdc, getNodeMask())) {
		printf("Llano.cpp::setAltVID - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * AltVid is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xDC
	 * bits from 0 to 6
	 */

	pciRegObject->setBits(0, 7, altVid);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano.cpp::setAltVID - unable to write to PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;
}

// CPU Usage module

bool Llano::getPsiEnabled() {

	PCIRegObject *pciRegObject;
	DWORD psiEnabled;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa0, getNodeMask())) {
		printf("Llano.cpp::getPsiEnabled - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * Psi bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa0
	 * bit 7
	 */

	psiEnabled = pciRegObject->getBits(0, 7, 1);

	free(pciRegObject);

	return (bool) psiEnabled;

}

DWORD Llano::getPsiThreshold() {

	PCIRegObject *pciRegObject;
	DWORD psiThreshold;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa0, getNodeMask())) {
		printf("Llano.cpp::getPsiThreshold - unable to read PCI register\n");
		free(pciRegObject);
		return false;
	}

	/*
	 * Psi threshold is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa0
	 * bits from 0 to 6
	 */

	psiThreshold = pciRegObject->getBits(0, 0, 7);

	free(pciRegObject);

	return psiThreshold;

}

void Llano::setPsiEnabled(bool toggle) {

	PCIRegObject *pciRegObject;

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa0, getNodeMask())) {
		printf("Llano.cpp::setPsiEnabled - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * Psi bit is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa0
	 * bit 7
	 */

	pciRegObject->setBits(7, 1, toggle);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano.cpp::setPsiEnabled - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

void Llano::setPsiThreshold(DWORD threshold) {

	PCIRegObject *pciRegObject;

	if (threshold > minVID() || threshold < maxVID()) {
		printf("setPsiThreshold: value must be between %d and %d\n", minVID(),
				maxVID());
		return;
	}

	pciRegObject = new PCIRegObject();

	if (!pciRegObject->readPCIReg(PCI_DEV_NORTHBRIDGE, PCI_FUNC_MISC_CONTROL_3,
			0xa0, getNodeMask())) {
		printf("Llano.cpp::setPsiThreshold - unable to read PCI register\n");
		free(pciRegObject);
		return;
	}

	/*
	 * Psi threshold is stored in PCI register with
	 * device PCI_DEV_NORTHBRIDGE
	 * function PC_FUNC_MISC_CONTROL_3
	 * register 0xa0
	 * bits from 0 to 6
	 */

	pciRegObject->setBits(0, 7, threshold);

	if (!pciRegObject->writePCIReg()) {
		printf("Llano.cpp::setPsiThreshold - unable to write PCI register\n");
		free(pciRegObject);
		return;
	}

	free(pciRegObject);

	return;

}

// Various settings

/* Register CMPHALT_REG is completely non-existent on Llano documentation. TODO: needs to be verified */
bool Llano::getC1EStatus() {

	MSRObject *msrObject;
	DWORD c1eBit;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(CMPHALT_REG, getMask())) {
		printf("Llano.cpp::getC1EStatus - unable to read MSR\n");
		free(msrObject);
		return false;
	}

	//Returns data for the first cpu in cpuMask (cpu 0)
	//C1E bit is stored in bit 28
	c1eBit = msrObject->getBitsLow(0, 28, 1);

	free(msrObject);

	return (bool) c1eBit;

}

void Llano::setC1EStatus(bool toggle) {

	MSRObject *msrObject;

	msrObject = new MSRObject();

	if (!msrObject->readMSR(CMPHALT_REG, getMask())) {
		printf("Llano.cpp::setC1EStatus - unable to read MSR\n");
		free(msrObject);
		return;
	}

	msrObject->setBitsLow(28, 1, toggle);

	//C1E bit is stored in bit 28
	if (!msrObject->writeMSR()) {
		printf("Llano.cpp::setC1EStatus - unable to write MSR\n");
		free(msrObject);
		return;
	}

	free(msrObject);

	return;

}

// Performance Counters

/*
 * Will show some informations about performance counter slots
 */
void Llano::perfCounterGetInfo() {

	Llano::K10PerformanceCounters::perfCounterGetInfo(this);
}

/*
 * perfCounterGetValue will retrieve and show the performance counter value for all the selected nodes/processors
 *
 */
void Llano::perfCounterGetValue(unsigned int perfCounter) {

	PerformanceCounter *performanceCounter;

	performanceCounter = new PerformanceCounter(getMask(), perfCounter, 4);

	if (!performanceCounter->takeSnapshot()) {
		printf(
				"Llano.cpp::perfCounterGetValue - unable to read performance counter");
		free(performanceCounter);
		return;
	}

	printf("Performance counter value: (decimal)%ld (hex)%lx\n",
			performanceCounter->getCounter(0),
			performanceCounter->getCounter(0));

}

void Llano::perfMonitorCPUUsage() {

	Llano::K10PerformanceCounters::perfMonitorCPUUsage(this);

}

void Llano::perfMonitorFPUUsage () {

	Llano::K10PerformanceCounters::perfMonitorFPUUsage(this);

}

void Llano::perfMonitorDCMA () {

	Llano::K10PerformanceCounters::perfMonitorDCMA(this);

}

void Llano::getCurrentStatus(struct procStatus *pStatus, DWORD core) {
	DWORD eaxMsr, edxMsr;

	RdmsrPx(0xc0010071, &eaxMsr, &edxMsr, (DWORD_PTR) 1 << core);
	pStatus->pstate = (eaxMsr >> 16) & 0x7;
	pStatus->vid = (eaxMsr >> 9) & 0x7f;
	pStatus->fid = eaxMsr & 0x3f;
	pStatus->did = (eaxMsr >> 6) & 0x7;

	return;

}

void Llano::checkMode() {
	DWORD i, pstate, vid, fid, did;
	DWORD eaxMsr, edxMsr;
	DWORD timestamp;
	DWORD states[2][8];
	DWORD minTemp, maxTemp, temp;
	DWORD oTimeStamp;
	float curVcore;
	DWORD maxPState;
	unsigned int cid;

	printf("Monitoring...\n");

	maxPState = getMaximumPState().getPState();

	for (i = 0; i < 8; i++) {
		states[0][i] = 0;
		states[1][i] = 0;
	}

	minTemp = getTctlRegister();
	maxTemp = minTemp;
	oTimeStamp = GetTickCount();

	while (1) {

		timestamp = GetTickCount();

		printf(" \rTs:%d - ", timestamp);
		for (i = 0; i < processorCores; i++) {

			/*RdmsrPx (0xc0010063,&eaxMsr,&edxMsr,i+1);
			 pstate=eaxMsr & 0x7;*/

			RdmsrPx(0xc0010071, &eaxMsr, &edxMsr, (PROCESSORMASK) 1 << i);
			pstate = (eaxMsr >> 16) & 0x7;
			vid = (eaxMsr >> 9) & 0x7f;
			curVcore = (float) ((124 - vid) * 0.0125);
			fid = eaxMsr & 0x3f;
			did = (eaxMsr >> 6) & 0x7;

			states[i][pstate]++;

			printf("c%d:ps%d - ", i, pstate);
			if (pstate > maxPState)
				printf("\n * Detected pstate %d on core %d\n", pstate, i);
		}

		temp = getTctlRegister();

		if (temp < minTemp)
			minTemp = temp;
		if (temp > maxTemp)
			maxTemp = temp;

		printf("Tctl: %d", temp);

		if ((timestamp - oTimeStamp) > 30000) {
			oTimeStamp = timestamp;

			printf("\n\tps0\tps1\tps2\tps3\tps4\n\n");
			for (cid = 0; cid < processorCores; cid++) {
				printf("Core%d:", cid);
				for (i = 0; i < 5; i++)
					printf("\t%d", states[cid][i]);

				printf("\n");
			}

			printf("\n\nCurTctl:%d\t MinTctl:%d\t MaxTctl:%d\n", temp, minTemp,
					maxTemp);

		}

		Sleep(50);
	}

	return;
}

/******** DRAM TIMINGS ***********/

/*
 * dram bank is valid
 */
bool Llano::getDramValid(DWORD device) {

	PCIRegObject *dramConfigurationHighRegister = new PCIRegObject();

	bool reg1;
	unsigned int offset;

	dramConfigurationHighRegister = new PCIRegObject();

	if (device == 0)
		offset = 0x0;
	else if (device == 1)
		offset = 0x100;

	reg1 = dramConfigurationHighRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x94 + offset, getNodeMask());

	if (!reg1) {
		printf("Llano::getDramValid - unable to read PCI registers\n");
		free(dramConfigurationHighRegister);
		return false;
	}

	return dramConfigurationHighRegister->getBits(0, 3, 1);

}

int Llano::getDramFrequency(DWORD device) {

	PCIRegObject *dramConfigurationHighRegister = new PCIRegObject();

	bool reg1;
	DWORD regValue;

	dramConfigurationHighRegister = new PCIRegObject();

	unsigned int offset;

	if (device == 0)
		offset = 0x0;
	else if (device == 1)
		offset = 0x100;

	reg1 = dramConfigurationHighRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x94 + offset, getNodeMask());

	if (!reg1) {
		printf("Llano::getDRAMFrequency - unable to read PCI registers\n");
		free(dramConfigurationHighRegister);
		return false;
	}

	regValue = dramConfigurationHighRegister->getBits(0, 0, 5);

	switch (regValue) {
	case 0x6:
		return 400;
	case 0xa:
		return 533;
	case 0xe:
		return 667;
	case 0x12:
		return 800;
	case 0x16:
		return 933;
	default:
		return 0;
	}

	return 0;

}

void Llano::getDramTimingHigh(DWORD device, DWORD *TrwtWB, DWORD *TrwtTO,
		DWORD *Twrrd, DWORD *Twrwr, DWORD *Trdrd, DWORD *Tref, DWORD *Trfc0,
		DWORD *Trfc1, DWORD *MaxRdLatency) {

	PCIRegObject *dramTimingHighRegister = new PCIRegObject();
	PCIRegObject *dramControlRegister = new PCIRegObject();

	bool reg1;
	bool reg2;

	unsigned int offset;

	if (device == 0)
		offset = 0x0;
	else if (device == 1)
		offset = 0x100;

	reg1 = dramTimingHighRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x8c + offset, getNodeMask());
	reg2 = dramControlRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x78 + offset, getNodeMask());

	if (!(reg1 && reg2)) {
		printf("Llano::getDRAMTimingHigh - unable to read PCI registers\n");
		free(dramTimingHighRegister);
		free(dramControlRegister);
		return;
	}

	*MaxRdLatency = dramControlRegister->getBits(0, 22, 10); //(DRAMControlReg >> 22) & 0x3ff;

	*TrwtWB = dramTimingHighRegister->getBits(0, 0, 4); //(miscReg >> 0) & 0x0f;
	*TrwtTO = dramTimingHighRegister->getBits(0, 4, 4); //(miscReg >> 4) & 0x0f;
	*Twrrd = dramTimingHighRegister->getBits(0, 10, 2); //(miscReg >> 10) & 0x03;
	*Twrwr = dramTimingHighRegister->getBits(0, 12, 2); //(miscReg >> 12) & 0x03;
	*Trdrd = dramTimingHighRegister->getBits(0, 14, 2); //(miscReg >> 14) & 0x03;
	*Tref = dramTimingHighRegister->getBits(0, 16, 2); //(miscReg >> 16) & 0x03;
	*Trfc0 = dramTimingHighRegister->getBits(0, 20, 3); //(miscReg >> 20) & 0x07;
	*Trfc1 = dramTimingHighRegister->getBits(0, 23, 3); //(miscReg >> 23) & 0x07;

	//Adjusting timings for DDR3/DDR2-1066 memories.

	*TrwtWB += 0;
	*TrwtTO += 2;

	*Twrrd += (dramControlRegister->getBits(0, 8, 2) << 2) + 1;
	*Twrwr += (dramControlRegister->getBits(0, 10, 2) << 2) + 1;
	*Trdrd += (dramControlRegister->getBits(0, 12, 2) << 2) + 2;

	free(dramTimingHighRegister);
	free(dramControlRegister);

	return;
}

void Llano::getDramTimingLow(
		DWORD device, // 0 or 1   DCT0 or DCT1
		DWORD *Tcl, DWORD *Trcd, DWORD *Trp, DWORD *Trtp, DWORD *Tras,
		DWORD *Trc, DWORD *Twr, DWORD *Trrd, DWORD *Tcwl, DWORD *T_mode,
		DWORD *Twtr, DWORD *Tfaw) {

	unsigned int offset;
	bool reg1;
	bool reg2;
	bool reg3;
	bool reg4;
	bool reg5;
	bool reg6, reg7, reg8;

	PCIRegObject *dramTimingLowRegister = new PCIRegObject();
	PCIRegObject *dramConfigurationHighRegister = new PCIRegObject();
	PCIRegObject *dramMsrRegister = new PCIRegObject();
	PCIRegObject *dramExtraDataOffset = new PCIRegObject();
	PCIRegObject *dramTiming0 = new PCIRegObject();
	PCIRegObject *dramTiming1 = new PCIRegObject();

	if (device == 0)
		offset = 0x0;
	else if (device == 1)
		offset = 0x100;

	reg1 = dramMsrRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x84 + offset, getNodeMask());
	reg2 = dramTimingLowRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x88 + offset, getNodeMask());
	reg3 = dramConfigurationHighRegister->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0x94 + offset, getNodeMask());

	/*
	 * Reads DRAM Timing Register 0 through extra data port F0/1F0 (view BKDG page
	 * D18F2x[1,0]F0 DRAM Controller Extra Data Offset Register page 287 for
	 * reference)
	 */
	reg4 = dramExtraDataOffset->readPCIReg(PCI_DEV_NORTHBRIDGE,
			PCI_FUNC_DRAM_CONTROLLER, 0xf0 + offset, getNodeMask());

	if (reg4) {

		dramExtraDataOffset->setBits(0, 28, 0x40); //0x40 is the register for DRAM Timing 0
		dramExtraDataOffset->setBits(30, 1, 0x0);

		reg5 = dramExtraDataOffset->writePCIReg();

		if (reg5)
			reg7 = dramTiming0->readPCIReg(PCI_DEV_NORTHBRIDGE,
					PCI_FUNC_DRAM_CONTROLLER, 0xf4 + offset, getNodeMask());

		dramExtraDataOffset->setBits(0, 28, 0x41); //0x41 is the register for DRAM timing 1
		dramExtraDataOffset->setBits(30, 1, 0x0);

		reg6 = dramExtraDataOffset->writePCIReg();

		if (reg6)
			reg8 = dramTiming1->readPCIReg(PCI_DEV_NORTHBRIDGE,
					PCI_FUNC_DRAM_CONTROLLER, 0xf4 + offset, getNodeMask());

	}

	if (!(reg1 && reg2 && reg3 && reg4 && reg5 && reg6 && reg7 && reg8)) {
		printf("LLano.cpp::getDRAMTimingLow - unable to read PCI register\n");
		free(dramMsrRegister);
		free(dramTimingLowRegister);
		free(dramConfigurationHighRegister);
		free(dramExtraDataOffset);
		free(dramTiming0);
		free(dramTiming1);
		return;
	}

	if (dramConfigurationHighRegister->getBits(0, 20, 1)) {
		*T_mode = 2;
	} else {
		*T_mode = 1;
	}

	// 0 = no tFAW window restriction
	// 1b= 16 memclk .... 1001b= 32 memclk
	*Tfaw = dramConfigurationHighRegister->getBits(0, 28, 4) << 1; //((miscReg >> 28) << 1);
	if (*Tfaw != 0)
		*Tfaw += 14;

	if (dramConfigurationHighRegister->getBits(0, 14, 1)) {
		printf("interface disabled on node %u DCT %u\n", selectedNode, device);
		return;
	}

	*Tcl = dramTimingLowRegister->getBits(0, 0, 4); //(miscReg) & 0x0F;
	*Trcd = dramTiming0->getBits(0, 0, 4);
	*Trp = dramTiming0->getBits(0, 8, 4);
	*Trtp = dramTiming1->getBits(0, 0, 3);
	*Tras = dramTiming0->getBits(0, 16, 5);
	*Trc = dramTiming0->getBits(0, 24, 6);
	*Trrd = dramTiming1->getBits(0, 8, 3);
	*Twtr = dramTiming1->getBits(0, 16, 3);
	*Twr = dramMsrRegister->getBits(0, 4, 3); //(miscReg >> 4) & 0x07; // assumes ddr3
	*Tcwl = dramMsrRegister->getBits(0, 20, 3); //(miscReg >> 20) & 0x07;

	*Tcl += 4;
	*Tras += 15;
	*Trp += 5;
	*Trc += 16;
	*Trcd += 5;

	*Twtr += 4;
	*Trrd += 4;
	*Trtp += 4;

	*Tcwl += 5;

	if (*Twr == 0)
		*Twr = 16;
	else if (*Twr >= 1 && *Twr <= 3)
		*Twr += 4;
	else if (*Twr >= 4)
		*Twr *= 2;

	free(dramMsrRegister);
	free(dramTimingLowRegister);
	free(dramConfigurationHighRegister);
	free(dramExtraDataOffset);
	free(dramTiming0);
	free(dramTiming1);

	return;
}

/************** PUBLIC SHOW METHODS ******************/

void Llano::showHTLink() {

	printf("Hypertransport Status:\n");
	printf("This processor has no Hypertransport Links\n");

}

void Llano::showHTC() {

	int i;
	int nodes = getProcessorNodes();

	printf("Hardware Thermal Control Status:\n\n");

	if (HTCisCapable() != true) {
		printf("Processor is not HTC Capable\n");
		return;
	}

	for (i = 0; i < nodes; i++) {
		printf(" --- Node %u:\n", i);
		setNode(i);
		printf("HTC features enabled flag: ");
		if (HTCisEnabled() == true)
			printf("true. Hardware Thermal Control is enabled.\n");
		else
			printf("false. Hardware Thermal Control is disabled.\n");

		printf("HTC features currently active (means overheating): ");
		if (HTCisActive() == true)
			printf("true\n");
		else
			printf("false\n");

		printf("HTC features has been active (means overheated in past): ");
		if (HTChasBeenActive() == true)
			printf("true\n");
		else
			printf("false\n");

		printf("HTC parameters are locked: ");
		if (HTCLocked() == true)
			printf("true\n");
		else
			printf("false\n");

		printf("HTC Slew control: ");
		if (HTCSlewControl() == true)
			printf("by Tctl Slew register\n");
		else
			printf("by Tctl without Slew register\n");

		printf(
				"HTC Limit temperature (equal or above means overheating): %d\n",
				HTCTempLimit());
		printf(
				"HTC Hysteresis temperature (equal or below means no more overheating) : %d\n",
				HTCTempLimit() - HTCHystTemp());
		printf("HTC PState Limit: %d\n", HTCPStateLimit());
		printf("\n");
	}
}

void Llano::showDramTimings() {

	int nodes = getProcessorNodes();
	int node_index;
	int dct_index;
	DWORD Tcl, Trcd, Trp, Trtp, Tras, Trc, Twr, Trrd, Tcwl, T_mode;
	DWORD Tfaw, TrwtWB, TrwtTO, Twtr, Twrrd, Twrwr, Trdrd, Tref, Trfc0;
	DWORD Trfc1, MaxRdLatency;
	DWORD ddrFrequency;

	printf("DRAM Configuration Status\n\n");

	for (node_index = 0; node_index < nodes; node_index++) {

		setNode(node_index);
		printf("Node %u ---\n", node_index);

		for (dct_index = 0; dct_index < 2; dct_index++) {

			if (getDramValid(dct_index)) {

				ddrFrequency = getDramFrequency(dct_index) * 2;

				getDramTimingLow(dct_index, &Tcl, &Trcd, &Trp, &Trtp, &Tras,
						&Trc, &Twr, &Trrd, &Tcwl, &T_mode, &Twtr, &Tfaw);

				getDramTimingHigh(dct_index, &TrwtWB, &TrwtTO, &Twrrd, &Twrwr,
						&Trdrd, &Tref, &Trfc0, &Trfc1, &MaxRdLatency);

				printf("DCT%d: ", dct_index);
				printf("memory type: DDR3");
				printf(" frequency: %d MHz\n", ddrFrequency);

				printf(
						"Tcl=%u Trcd=%u Trp=%u Tras=%u Access Mode:%uT Trtp=%u Trc=%u Twr=%u Trrd=%u Tcwl=%u Tfaw=%u\n",
						Tcl, Trcd, Trp, Tras, T_mode, Trtp, Trc, Twr, Trrd,
						Tcwl, Tfaw);
				printf(
						"TrwtWB=%u TrwtTO=%u Twtr=%u Twrrd=%u Twrwr=%u Trdrd=%u Tref=%u Trfc0=%u Trfc1=%u MaxRdLatency=%u\n",
						TrwtWB, TrwtTO, Twtr, Twrrd, Twrwr, Trdrd, Tref, Trfc0,
						Trfc1, MaxRdLatency);

			} else {

				printf("- controller unactive -\n");
			}

		}

		printf("\n");

	} // while

	return;

}
