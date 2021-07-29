#include "wm_i2c.h"

#define I2C_MASTER_PORT 0
#define I2C_MASTER_SDA_IO GPIO_NUM_26 
#define I2C_MASTER_SCL_IO GPIO_NUM_18 
#define I2C_MASTER_FREQ_HZ 25000
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_ACK_CHECK_EN 0x1
#define I2C_ACK_CHECK_DIS 0x0

#define WM_ADDRESS 0x1a

static esp_err_t i2c_write(i2c_port_t i2c_num, uint8_t *data, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    esp_err_t err = i2c_master_write_byte(
        cmd,
        (WM_ADDRESS << 1) | I2C_MASTER_WRITE,
        I2C_ACK_CHECK_EN
    );
    ESP_ERROR_CHECK(err);
    err = i2c_master_write(
        cmd,
        data,
        size,
        I2C_ACK_CHECK_EN
    );
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(i2c_master_stop(cmd));
    esp_err_t ret = i2c_master_cmd_begin(
        i2c_num,
        cmd,
        portMAX_DELAY
    );
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t wm_write_reg(uint8_t reg, uint16_t data)
{
    uint8_t out[2];
    out[0] = (reg << 1) | (uint8_t)((data >> 8) & 0x0001);
    out[1] = (uint8_t)(data & 0x00FF);

    esp_err_t err = i2c_write(I2C_NUM_0, out, 2);
    return err;
}

void wm_i2c_init()
{
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_NUM_0, &conf);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, conf.mode,
        I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    ESP_ERROR_CHECK(err);

    //Reset
    err = wm_write_reg(0x0F, 0x0000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    
    //Set Power Source
    //Power Mgmt
    err = wm_write_reg(0x19, 1 << 8 | 1 << 7 | 1 << 6 | 1 << 4 | 1 << 2);
    err += wm_write_reg(0x1A, 1 << 8 | 1 << 7 | 1 << 3);
    err += wm_write_reg(0x2F, 1 << 4 | 1 << 2);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Clock
    err = wm_write_reg(0x04, 0x0000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Class D Speaker Outputs
    err = wm_write_reg(0x31, 1 << 7);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //ADC/DAC
    err = wm_write_reg(0x05, 0x0000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Inputs
    //R1 Right PGA Input Volume
    err = wm_write_reg(0x01, 0b100110000);
    //R33 ADCR Signal Path
    err += wm_write_reg(0x21, 0b100001000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Audio Interface
    //I2S Format 16-bit
    err = wm_write_reg(0x07, 0x0002);
    err += wm_write_reg(0x06, 0b110);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Speaker L & R Output Volumes
    err =  wm_write_reg(0x29, 0b101111000);
    //err += wm_write_reg(0x03, 0x00F9 | 0x0100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //DAC Volume
    err =  wm_write_reg(0x0a, 0xFFFF | 0x0100);
    err += wm_write_reg(0x0b, 0xFFFF | 0x0100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Mixer
    err = wm_write_reg(0x25, 0b100000000);
    err += wm_write_reg(0x2E, 0b010000000);
    //err += wm_write_reg(0x25, 1 << 8);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
}