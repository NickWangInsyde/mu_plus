/** @file -- MsWheaReportHER.c

This source implements backend routines to support storing persistent hardware 
error records in UEFI.

Copyright (C) Microsoft Corporation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
1. Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
this list of conditions and the following disclaimer in the documentation
and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

**/

#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/PrintLib.h>
#include "MsWheaReportHER.h"

/**

This routine accepts the pointer to the MS WHEA entry metadata, payload and payload length, allocate and
fill (AnF) in the buffer with supplied information. Then return the pointer of processed buffer.

Note: Caller is responsible for freeing the valid returned buffer!!!

@param[in]  MsWheaEntryMD             The pointer to reported MS WHEA error metadata
@param[out]  PayloadSize              The pointer to payload length, this will be updated to the totally
                                      allocated/operated size if operation succeeded

@retval NULL if any errors, otherwise filled and allocated buffer will be returned. 

**/
STATIC
VOID *
MsWheaAnFBuffer (
  IN MS_WHEA_ERROR_ENTRY_MD           *MsWheaEntryMD,
  IN OUT UINT32                       *PayloadSize
  )
{
  EFI_STATUS                      Status = EFI_SUCCESS;
  UINT8                           *Buffer = NULL;
  UINT32                          BufferIndex = 0;
  UINT32                          ErrorPayloadSize = 0;

  EFI_COMMON_ERROR_RECORD_HEADER  *CperHdr;
  EFI_ERROR_SECTION_DESCRIPTOR    *CperErrSecDscp;
  MU_TELEMETRY_CPER_SECTION_DATA  *MuTelemetryData;
  
  DEBUG((DEBUG_INFO, "%a: enter...\n", __FUNCTION__));
  
  if ((MsWheaEntryMD == NULL) || (PayloadSize == NULL)) {
    Status = EFI_INVALID_PARAMETER;
    goto Cleanup;
  }

  ErrorPayloadSize = sizeof(MU_TELEMETRY_CPER_SECTION_DATA);

  Buffer = AllocateZeroPool(sizeof(EFI_COMMON_ERROR_RECORD_HEADER) + 
                            sizeof(EFI_ERROR_SECTION_DESCRIPTOR) +
                            ErrorPayloadSize);
  if (Buffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto Cleanup;
  }

  // Grab pointers to each header
  CperHdr = (EFI_COMMON_ERROR_RECORD_HEADER*)&Buffer[BufferIndex];
  BufferIndex += sizeof(EFI_COMMON_ERROR_RECORD_HEADER);

  CperErrSecDscp = (EFI_ERROR_SECTION_DESCRIPTOR*)&Buffer[BufferIndex];
  BufferIndex += sizeof(EFI_ERROR_SECTION_DESCRIPTOR);

  MuTelemetryData = (MU_TELEMETRY_CPER_SECTION_DATA*)&Buffer[BufferIndex];
  BufferIndex += sizeof(MU_TELEMETRY_CPER_SECTION_DATA);

  // Fill out error type based headers according to UEFI Spec...
  CreateHeadersDefault(CperHdr, 
                      CperErrSecDscp, 
                      MuTelemetryData, 
                      MsWheaEntryMD, 
                      ErrorPayloadSize);

  // Update PayloadSize as the recorded error has Headers and Payload merged
  *PayloadSize = BufferIndex;

 Cleanup:
  DEBUG((DEBUG_INFO, "%a: exit %r...\n", __FUNCTION__, Status));
  return (VOID*) Buffer;
}

/**

This routine accepts the pointer to a UINT16 number. It will iterate through each HwErrRecXXXX and stops 
after 0xFFFF iterations or spotted a slot that returns EFI_NOT_FOUND. 

@param[out]  next                       The pointer to output result holder

@retval EFI_SUCCESS                     Entry addition is successful.
@retval EFI_INVALID_PARAMETER           Input pointer is NULL.
@retval EFI_OUT_OF_RESOURCES            No available slot for HwErrRec.
@retval Others                          See GetVariable for more details

**/
STATIC
EFI_STATUS
MsWheaFindNextAvailableSlot (
  OUT UINT16      *next
  )
{
  EFI_STATUS        Status = EFI_SUCCESS;
  UINT32            Index = 0;
  UINTN             Size = 0;
  CHAR16            VarName[EFI_HW_ERR_REC_VAR_NAME_LEN];

  if (next == NULL) {
    Status = EFI_INVALID_PARAMETER;
    goto Cleanup;
  }

  if ((gRT == NULL) || (gRT->GetVariable == NULL)) {
    Status = EFI_NOT_READY;
    goto Cleanup;
  }

  for (Index = 0; Index <= MAX_UINT16; Index++) {
    Size = 0;
    UnicodeSPrint(VarName, sizeof(VarName), L"%s%04X", EFI_HW_ERR_REC_VAR_NAME, (UINT16)(Index & MAX_UINT16));
    Status = gRT->GetVariable(VarName,
                              &gEfiHardwareErrorVariableGuid,
                              NULL,
                              &Size,
                              NULL);
    if (Status == EFI_NOT_FOUND) {
      break;
    }
  }
  // Translate result corresponds to this specific function
  if (Status == EFI_NOT_FOUND) {
    *next = (UINT16)(Index & MAX_UINT16);
    Status = EFI_SUCCESS;
  } else if (Status == EFI_SUCCESS) {
    Status = EFI_OUT_OF_RESOURCES;
  } else {
    // Do Nothing, pass on the error
  }

Cleanup:
  return Status;
}

/**

Clear all the HwErrRec entries on flash.

@retval EFI_SUCCESS                     Entry addition is successful.
@retval Others                          See GetVariable/SetVariable for more details

**/
EFI_STATUS
EFIAPI
MsWheaClearAllEntries (
  VOID
  )
{
  UINT32                Index = 0;
  CHAR16                VarName[EFI_HW_ERR_REC_VAR_NAME_LEN];
  UINTN                 Size = 0;
  EFI_STATUS            Status = EFI_SUCCESS;
  UINT32                Attributes;
  
  DEBUG((DEBUG_ERROR, "%a enter\n", __FUNCTION__));

  for (Index = 0; Index <= MAX_UINT16; Index++) {
    Size = 0;
    UnicodeSPrint(VarName, sizeof(VarName), L"%s%04X", EFI_HW_ERR_REC_VAR_NAME, Index);
    Status = gRT->GetVariable(VarName,
                              &gEfiHardwareErrorVariableGuid,
                              NULL,
                              &Size,
                              NULL);
    if (Status == EFI_NOT_FOUND) {
      // Do nothing
      continue;
    }
    else if (Status != EFI_BUFFER_TOO_SMALL) {
      // We have other problems here..
      break;
    }

    Attributes = EFI_VARIABLE_NON_VOLATILE |
                 EFI_VARIABLE_BOOTSERVICE_ACCESS |
                 EFI_VARIABLE_RUNTIME_ACCESS;

    DEBUG((DEBUG_VERBOSE, "Attributes for variable %s: %x\n", VarName, Attributes));

    if (PcdGetBool(PcdVariableHardwareErrorRecordAttributeSupported)) {
      Attributes |= EFI_VARIABLE_HARDWARE_ERROR_RECORD;
    }

    Status = gRT->SetVariable(VarName, &gEfiHardwareErrorVariableGuid, Attributes, 0, NULL);

    if (EFI_ERROR(Status) != FALSE) {
      DEBUG((DEBUG_ERROR, "%a Clear HwErrRec has an issue - %r\n", __FUNCTION__, Status));
      break;
    }
  }

  if ((Status == EFI_SUCCESS) || (Status == EFI_NOT_FOUND)) {
    Status = EFI_SUCCESS;
  }
  
  DEBUG((DEBUG_ERROR, "%a exit...\n", __FUNCTION__));
  return Status;
}

/**

This routine accepts the pointer to the MS WHEA entry metadata, error specific data payload and its size 
then store on the flash as HwErrRec awaiting to be picked up by OS (Refer to UEFI Spec 2.7A)

@param[in]  MsWheaEntryMD             The pointer to reported MS WHEA error metadata

@retval EFI_SUCCESS                   Entry addition is successful.
@retval EFI_INVALID_PARAMETER         Input has NULL pointer as input.
@retval EFI_OUT_OF_RESOURCES          Not enough spcae for the requested space.

**/
EFI_STATUS
EFIAPI
MsWheaReportHERAdd (
  IN MS_WHEA_ERROR_ENTRY_MD           *MsWheaEntryMD
  )
{
  EFI_STATUS        Status = EFI_SUCCESS;
  UINT16            Index = 0;
  VOID              *Buffer = NULL;
  UINT32            Size = 0;
  CHAR16            VarName[EFI_HW_ERR_REC_VAR_NAME_LEN];
  UINT32            Attributes;
  
  // 1. Find an available variable name for next write
  Status = MsWheaFindNextAvailableSlot(&Index);
  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "%a: Find the next available slot failed (%r)\n", __FUNCTION__, Status));
    goto Cleanup;
  }

  // 2. Fill out headers
  Buffer = MsWheaAnFBuffer(MsWheaEntryMD, &Size);
  if (Buffer == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    DEBUG((DEBUG_ERROR, "%a: Buffer allocate and fill failed (%r)\n", __FUNCTION__, Status));
    goto Cleanup;
  }
  else if (Size == 0) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG((DEBUG_ERROR, "%a: Buffer filling returned 0 length...\n", __FUNCTION__));
    goto Cleanup;
  }

  // 3. Save the record to flash
  UnicodeSPrint(VarName, sizeof(VarName), L"%s%04X", EFI_HW_ERR_REC_VAR_NAME, Index);

  Attributes = EFI_VARIABLE_NON_VOLATILE |
               EFI_VARIABLE_BOOTSERVICE_ACCESS |
               EFI_VARIABLE_RUNTIME_ACCESS;

  if (PcdGetBool(PcdVariableHardwareErrorRecordAttributeSupported)) {
    Attributes |= EFI_VARIABLE_HARDWARE_ERROR_RECORD;
  }

  DEBUG((DEBUG_VERBOSE, "Attributes for variable %s: %x\n", VarName, Attributes));

  Status = gRT->SetVariable(VarName, &gEfiHardwareErrorVariableGuid, Attributes, Size, Buffer);

  if (EFI_ERROR(Status)) {
    DEBUG((DEBUG_ERROR, "%a: Write size of %d at index %04X errored with (%r)\n", __FUNCTION__, Size, Index, Status));
  } else {
    DEBUG((DEBUG_INFO, "%a: Write size of %d at index %04X succeeded\n", __FUNCTION__, Size, Index));
  }

Cleanup:
  if (Buffer) {
    FreePool(Buffer);
  }
  DEBUG((DEBUG_INFO, "%a: exit (%r)\n", __FUNCTION__, Status));
  return Status;
}
