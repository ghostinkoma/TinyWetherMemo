// ============================================================================
//  io_task.h  -  I2C を単独所有する FreeRTOS タスク (雷ブリッジ + 環境センサ)
//
//  雷 poll と全センサ read(ブロック可)をこのタスクだけで行い、結果を g_state へ
//  publish する。ブロッキングは vTaskDelay として CPU を譲るため WiFi/loop を
//  止めない。I2C に触るのは本タスクのみ = バス競合なし。
// ============================================================================
#ifndef WLB_IO_TASK_H
#define WLB_IO_TASK_H

// IOタスクを起動 (setup から一度呼ぶ)。g_state.begin() 済みであること。
void iotask_start();

#endif // WLB_IO_TASK_H
