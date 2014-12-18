/*
*  Copyright (C) 2014 Orbitech
*
*  Authors: xqrzd
*
*  This program is free software; you can redistribute it and/or modify
*  it under the terms of the GNU General Public License version 2 as
*  published by the Free Software Foundation.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program; if not, write to the Free Software
*  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
*  MA 02110-1301, USA.
*/

#include "Ntfs.h"
#include <stdio.h>

PVOID FORCEINLINE NtfspAllocate(
	_In_ SIZE_T Size)
{
#ifdef _DEBUG
	return malloc(Size);
#else
	return HeapAlloc(GetProcessHeap(), HEAP_GENERATE_EXCEPTIONS, Size);
#endif
}

VOID FORCEINLINE NtfspFree(
	_In_ PVOID Buffer)
{
#ifdef _DEBUG
	free(Buffer);
#else
	HeapFree(GetProcessHeap(), 0, Buffer);
#endif
}

BOOLEAN NtfsInitVolume(
	_In_ PNTFS_READ_SECTOR NtfsReadSector,
	_In_ USHORT BytesPerSector,
	_In_ PVOID Context,
	_Out_ PNTFS_VOLUME NtfsVolume)
{
	BOOLEAN success = FALSE;

	PNTFS_BOOT_SECTOR bootSector = NtfspAllocate(BytesPerSector);

	NtfsVolume->NtfsReadSector = NtfsReadSector;
	NtfsVolume->BytesPerSector = BytesPerSector;
	NtfsVolume->Context = Context;

	if (NtfsReadSector(NtfsVolume, 0, 1, bootSector))
	{
		if (NtfsIsVolumeValid(bootSector))
		{
			NtfsVolume->BytesPerSector = bootSector->BytesPerSector;
			NtfsVolume->SectorsPerCluster = bootSector->SectorsPerCluster;
			NtfsVolume->FileRecordSize = NtfsClustersToBytes(bootSector->ClustersPerFileRecord, bootSector->BytesPerSector, bootSector->SectorsPerCluster);
			NtfsVolume->IndexBlockSize = NtfsClustersToBytes(bootSector->ClustersPerIndexBlock, bootSector->BytesPerSector, bootSector->SectorsPerCluster);
			NtfsVolume->MftStartSector = bootSector->MFT_LCN * bootSector->SectorsPerCluster;

			printf("Init NTFS Volume\n");
			printf("\tBytesPerSector: %u\n", NtfsVolume->BytesPerSector);
			printf("\tSectorsPerCluster: %u\n", NtfsVolume->SectorsPerCluster);
			printf("\tFile record size: %u\n", NtfsVolume->FileRecordSize);
			printf("\tIndex block size: %u\n", NtfsVolume->IndexBlockSize);
			printf("\tMFT start sector: %llu\n", NtfsVolume->MftStartSector);
		}
		else
			printf("NtfsInitVolume: Volume isn't NTFS [OemId: %s] [EndMarker: %X]\n", bootSector->OemId, bootSector->EndMarker);
	}
	else
		printf("NtfsInitVolume: NtfsReadSector failed to read boot sector\n");

	NtfspFree(bootSector);

	return success;
}

// Notes: FileRecord must be at least the size of NTFS_VOLUME.FileRecordSize
BOOLEAN NtfsReadFileRecord(
	_In_ PNTFS_VOLUME NtfsVolume,
	_In_ ULONG RecordNumber,
	_Out_ PNTFS_FILE_RECORD FileRecord)
{
	BOOLEAN success = FALSE;
	BOOLEAN found = FALSE;

	ULONG fileRecordSizeInSectors = NtfsVolume->FileRecordSize / NtfsVolume->BytesPerSector;
	ULONGLONG fileRecordStartSector = 0;

	if (RecordNumber <= MFT_RECORD_USER)
	{
		fileRecordStartSector = NtfsVolume->MftStartSector;
		found = TRUE;
	}
	else
	{
		// TODO: walk through MFT data runs.
	}

	if (found)
	{
		// Read file record
		if (NtfsVolume->NtfsReadSector(NtfsVolume, fileRecordStartSector, fileRecordSizeInSectors, FileRecord))
		{
			if (FileRecord->Magic == FILE_RECORD_MAGIC /*&& NtfsFileExists(FileRecord)*/)
			{
				PUSHORT usnAddress = NtfsOffsetToPointer(FileRecord, FileRecord->UpdateSequenceOffset);
				success = NtfsPatchUpdateSequence(NtfsVolume, (PUSHORT)FileRecord, fileRecordSizeInSectors, usnAddress);

				if (!success)
					printf("NtfsReadFileRecord: NtfsPatchUpdateSequence failed for record %u\n", RecordNumber);
			}
			else
				printf("NtfsReadFileRecord: File record %u is invalid\n", RecordNumber);
		}
		else
			printf("NtfsReadFileRecord: Unable to find record %u\n", RecordNumber);
	}
	else
		printf("NtfsReadFileRecord: Unable to find record %u\n", RecordNumber);

	return success;
}

BOOLEAN NtfsPatchUpdateSequence(
	_In_ PNTFS_VOLUME NtfsVolume,
	_Inout_ PUSHORT Sector,
	_In_ ULONG SectorCount,
	_In_ PUSHORT UsnAddress)
{
	USHORT usn = *UsnAddress;
	PUSHORT usArray = UsnAddress + 1;
	ULONG i;

	for (i = 0; i < SectorCount; i++)
	{
		Sector += ((NtfsVolume->BytesPerSector >> 1) - 1);

		if (*Sector != usn)
			return FALSE;

		*Sector = usArray[i]; // Write back correct data
		Sector++;
	}

	return TRUE;
}