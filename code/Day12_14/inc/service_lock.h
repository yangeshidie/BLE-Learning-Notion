#ifndef SERVICE_LOCK_H
#define SERVICE_LOCK_H

#include <stdbool.h>

/**
 * @brief 更新锁的状态通知给手机
 * @param is_unlocked true=开锁状态, false=关锁状态
 */
int service_lock_send_status(bool is_unlocked);

#endif // SERVICE_LOCK_H