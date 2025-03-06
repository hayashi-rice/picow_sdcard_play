/* main.c
Copyright 2021 Carl John Kugler III

Licensed under the Apache License, Version 2.0 (the License); you may not use
this file except in compliance with the License. You may obtain a copy of the
License at

   http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software distributed
under the License is distributed on an AS IS BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied. See the License for the
specific language governing permissions and limitations under the License.
*/

#include <stdio.h>
#include "pico/stdlib.h"
#include "hw_config.h"
#include "f_util.h"
#include "ff.h"

#define BUF_SIZE 1024

unsigned char buffer[BUF_SIZE];
UINT br,bw;

/**
 * @file main.c
 * @brief Minimal example of writing to a file on SD card
 * @details
 * This program demonstrates the following:
 * - Initialization of the stdio
 * - Mounting and unmounting the SD card
 * - Opening a file and writing to it
 * - Closing a file and unmounting the SD card
 */

int main() {
    // Initialize stdio
    stdio_init_all();

    sleep_ms(5000); 

    puts("Conected");
    puts("Hello, world!");

    // See FatFs - Generic FAT Filesystem Module, "Application Interface",
    // http://elm-chan.org/fsw/ff/00index_e.html
    FATFS fs[2];
    FRESULT fr;

    fr = f_mount(&fs[0], "", 1);
    if (FR_OK != fr) {
        panic("f_mount error file1: %s (%d)\n", FRESULT_str(fr), fr);
    }
    fr = f_mount(&fs[1], "", 1);
    if (FR_OK != fr) {
        panic("f_mount error file2: %s (%d)\n", FRESULT_str(fr), fr);
    }

    // Open a file and write to it
    FIL fil1, fil2;
    const char* const readfile = "Recording_Romanian_2.wsd";
    const char* const writefile = "output_data.txt";

    fr = f_open(&fil1, readfile, FA_OPEN_EXISTING | FA_READ);
    if (FR_OK != fr && FR_EXIST != fr) {
        panic("f_open(%s) error file1: %s (%d)\n", readfile, FRESULT_str(fr), fr);
    }
    fr = f_open(&fil2, writefile, FA_CREATE_ALWAYS | FA_WRITE);
    if (FR_OK != fr && FR_EXIST != fr) {
        panic("f_open(%s) error file2: %s (%d)\n", writefile, FRESULT_str(fr), fr);
    }

    //char hexBuffer[BUF_SIZE * 2 + 1];  // 1バイトにつき最大2文字（2桁＋スペース）、さらに改行用のバッファを用意
    for (;;) {
        fr = f_read(&fil1, buffer, BUF_SIZE, &br);
        if (fr != FR_OK || br == 0) break;   /* エラーかファイル終端 */
        fr = f_write(&fil2, buffer, br, &bw);
        if (fr != FR_OK || bw < br*2) break;   /* エラーかディスク満杯 */
    }

    // Close the file 
    f_close(&fil1);
    //if (FR_OK != fr) {
        //printf("f_close error file1: %s (%d)\n", FRESULT_str(fr), fr);
    //}
    f_close(&fil2);
    //if (FR_OK != fr) {
        //printf("f_close error file2: %s (%d)\n", FRESULT_str(fr), fr);
    //}

    // Unmount the SD card
    f_unmount("");

    puts("finish!");
    for (;;);
}
