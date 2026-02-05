#include "service_lock.h"
#include "app_lock.h"
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(service_lock, LOG_LEVEL_INF);

#define LOCK_SERVICE_UUID_VAL 0x180A
#define LOCK_CONTROL_CHAR_UUID_VAL 0x2A57
#define LOCK_STATUS_CHAR_UUID_VAL 0x2A58

static struct bt_uuid_128 lock_service_uuid = BT_UUID_INIT_128(
    0x01, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF, 0xF0
);

static struct bt_uuid_128 lock_control_uuid = BT_UUID_INIT_128(
    0x02, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF, 0xF0
);

static struct bt_uuid_128 lock_status_uuid = BT_UUID_INIT_128(
    0x03, 0x12, 0x23, 0x34, 0x45, 0x56, 0x67, 0x78,
    0x89, 0x9A, 0xAB, 0xBC, 0xCD, 0xDE, 0xEF, 0xF0
);

static uint8_t lock_status_value = 0;

static ssize_t read_lock_status(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                void *buf, uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                              &lock_status_value, sizeof(lock_status_value));
}

static ssize_t write_lock_control(struct bt_conn *conn,
                                  const struct bt_gatt_attr *attr,
                                  const void *buf, uint16_t len,
                                  uint16_t offset, uint8_t flags)
{
    const uint8_t *value = buf;

    if (offset > 0) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    LOG_INF("Received lock control command: 0x%02X", value[0]);

    if (value[0] == 0x01) {
        app_lock_execute_action(LOCK_ACTION_UNLOCK);
    } else if (value[0] == 0x00) {
        app_lock_execute_action(LOCK_ACTION_LOCK);
    } else {
        LOG_WRN("Unknown lock command: 0x%02X", value[0]);
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    return len;
}

static void lock_status_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                        uint16_t value)
{
    LOG_INF("Lock Status CCCD changed: %d", value);
}

BT_GATT_SERVICE_DEFINE(lock_svc,
    BT_GATT_PRIMARY_SERVICE(&lock_service_uuid),
    BT_GATT_CHARACTERISTIC(&lock_control_uuid.uuid,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESPONSE,
                           BT_GATT_PERM_WRITE_ENCRYPT,
                           NULL, write_lock_control, NULL),
    BT_GATT_CHARACTERISTIC(&lock_status_uuid.uuid,
                           BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                           BT_GATT_PERM_READ_ENCRYPT,
                           read_lock_status, NULL, &lock_status_value),
    BT_GATT_CCC(lock_status_ccc_cfg_changed,
                BT_GATT_PERM_READ | BT_GATT_PERM_WRITE_ENCRYPT),
);

int service_lock_init(void)
{
    LOG_INF("Lock service initialized");
    return 0;
}

int service_lock_send_status(uint8_t status)
{
    int err;
    struct bt_conn *conn;

    lock_status_value = status;

    conn = bt_conn_lookup_state_le(NULL, BT_CONN_CONNECTED);
    if (!conn) {
        LOG_DBG("No connected peer to notify");
        return -ENOTCONN;
    }

    err = bt_gatt_notify(conn, &lock_svc.attrs[3], &lock_status_value,
                         sizeof(lock_status_value));
    bt_conn_unref(conn);

    if (err) {
        LOG_ERR("Failed to notify lock status (err %d)", err);
        return err;
    }

    LOG_INF("Lock status notified: %d", status);
    return 0;
}
