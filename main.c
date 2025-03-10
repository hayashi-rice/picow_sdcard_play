#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "pico/time.h"
#include "hw_config.h"
#include "f_util.h"
#include "ff.h"
#include "ds_out.pio.h"   // 生成されたPIOヘッダ（pioasmで生成）

// ピン定義
#define AUDIO_OUT_PIN 15    // オーディオ出力

// バッファサイズ（32bit単位で転送するので、サイズは4の倍数）
#define BUFFER_SIZE 4096

// ダブルバッファ用のデータ領域（4バイト境界に揃える）
static uint8_t __attribute__((aligned(4))) buffer0[BUFFER_SIZE];
static uint8_t __attribute__((aligned(4))) buffer1[BUFFER_SIZE];

//////////////////////////////////////////////////////////////////////////////
// PIOプログラム初期化関数
//////////////////////////////////////////////////////////////////////////////
void ds_out_program_init(PIO pio, uint sm, uint offset, uint pin) {
    pio_sm_config c = ds_out_program_get_default_config(offset);
    // 出力するピンを設定
    sm_config_set_set_pins(&c, pin, 1);
    // PIOの出力方向を設定
    pio_sm_set_consecutive_pindirs(pio, sm, pin, 1, true);
    // FIFOの自動プルを有効に（OSRが空になったら自動的にFIFOからデータをプル）
    sm_config_set_out_shift(&c, true, true, 32);
    // PIO SM初期化
    pio_sm_init(pio, sm, offset, &c);
    // SM有効化
    pio_sm_set_enabled(pio, sm, true);
}

//////////////////////////////////////////////////////////////////////////////
// メイン
//////////////////////////////////////////////////////////////////////////////
int main(void) {
    stdio_init_all();

    // FatFsでWSDファイル "audio.wsd" をオープン
    FIL audio_file;
    FRESULT fr = f_open(&audio_file, "audio.wsd", FA_READ);
    if (fr != FR_OK) {
        printf("Failed to open file: %d\n", fr);
        while(1) sleep_ms(1000);
    }
    printf("File opened.\n");

    // ------------------------------
    // PIOとStateMachineの初期化
    // ------------------------------
    PIO pio = pio0;
    uint sm = 0;
    // PIOプログラムをPIOに書き込み、オフセットを取得
    uint offset = pio_add_program(pio, &ds_out_program);
    // PIOプログラム初期化（オーディオ出力用GPIOに出力）
    ds_out_program_init(pio, sm, offset, AUDIO_OUT_PIN);

    // サンプルレートについて
    // 本プログラムは、1ビット出力ループが「out + jmp」の2命令で構成
    // 1ビットの出力周期 = 2 × (1 / SM実行周波数)
    // 2.8MHzのオーディオ出力を得るためには、SM実行周波数を5.6MHzに設定
    float sm_clk_div = (float)125e6 / 5.6e6;  // 約22.32
    sm_set_clkdiv(pio, sm, sm_clk_div);

    // ------------------------------
    // DMAの設定：メモリ（バッファ）→PIO TX FIFO
    // ------------------------------
    int dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    // 32bit単位の転送（※ FatFsから読み出すデータは1バイト単位だが、
    //     DMA転送では4バイトずつ送るので、ファイルサイズが4の倍数であることを想定）
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    // PIO TX FIFOのDREQを使用（TX FIFOが空になるとDMAが進行）
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    // 読み出し側はアドレスインクリメント、書き込み側は固定
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    // ------------------------------
    // 最初のバッファ読み込み（buffer0）とDMA転送開始
    // ------------------------------
    UINT bytes_read;
    fr = f_read(&audio_file, buffer0, BUFFER_SIZE, &bytes_read);
    if (fr != FR_OK || bytes_read == 0) {
        printf("Failed to read file or empty file.\n");
        while(1) sleep_ms(1000);
    }
    // DMA転送数は32bit単位なので、bytes_readは4で割れる前提
    uint transfers = bytes_read / 4;

    // DMA転送開始（buffer0からPIO TX FIFOへ）
    dma_channel_configure(
         dma_chan,
         &c,
         &pio->txf[sm],  // 書き込み先はPIOのTX FIFO
         buffer0,        // 読み出し元：buffer0
         transfers,
         true            // すぐにスタート
    );
    printf("Started DMA transfer from buffer0 (%u bytes, %u transfers).\n", bytes_read, transfers);

    // ------------------------------
    // ダブルバッファ方式で連続再生
    // ------------------------------
    bool use_buffer0 = true;
    while (1) {
        // DMA転送が完了するまで待つ（ブロッキング）
        dma_channel_wait_for_finish_blocking(dma_chan);

        // 次のブロックをSDカードから読み込み
        fr = f_read(&audio_file,
                    use_buffer0 ? buffer0 : buffer1,
                    BUFFER_SIZE, &bytes_read);
        if (fr != FR_OK) {
            printf("Error reading file: %d\n", fr);
            break;
        }
        if (bytes_read == 0) {
            // ファイル終端ならループを抜ける
            printf("End of file reached.\n");
            break;
        }
        transfers = bytes_read / 4;

        // DMA転送再設定（新たに読み込んだバッファから転送開始）
        dma_channel_configure(
             dma_chan,
             &c,
             &pio->txf[sm],
             use_buffer0 ? buffer0 : buffer1,
             transfers,
             true
        );

        // バッファを交互に使用
        use_buffer0 = !use_buffer0;
    }

    // 再生終了後、ファイルをクローズ
    f_close(&audio_file);
    printf("Playback finished.\n");

    while (1) {
        sleep_ms(1000);
    }
    return 0;
}
