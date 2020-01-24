// File author is Ítalo Lima Marconato Matias
//
// Created on June 28 of 2018, at 19:20 BRT
// Last edited on January 23 of 2020, at 21:37 BRT

#ifndef __CHICAGO_ARCH_PMM_H__
#define __CHICAGO_ARCH_PMM_H__

#include <chicago/mm.h>

extern PMmPageRegion MmPageRegions;
extern PUIntPtr MmPageReferences;
extern UIntPtr MmPageRegionCount;
extern UIntPtr MmUsedBytes;
extern UIntPtr MmMaxBytes;

extern UIntPtr KernelStart;

#ifndef __CHICAGO_PMM__
extern UIntPtr MmBootAllocPointer;
extern UIntPtr KernelRealEnd;
#endif

Void PMMInit(Void);

#endif		// __CHICAGO_ARCH_PMM_H__
