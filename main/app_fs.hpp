#pragma once

// SPIFFS mount helpers. The image lives in the "spiffs" partition (see
// partitions.csv) and is mounted at /spiffs. Unmount is needed before an OTA
// filesystem update rewrites the partition.

bool fs_mount();
void fs_unmount();
