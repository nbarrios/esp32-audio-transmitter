#include "es8388_i2c.h"

#define I2C_MASTER_PORT 0
#define I2C_MASTER_SDA_IO GPIO_NUM_18 
#define I2C_MASTER_SCL_IO GPIO_NUM_23 
#define I2C_MASTER_FREQ_HZ 50000
#define I2C_MASTER_RX_BUF_DISABLE 0
#define I2C_MASTER_TX_BUF_DISABLE 0
#define I2C_ACK_CHECK_EN 0x1
#define I2C_ACK_CHECK_DIS 0x0

#define ES_ADDRESS 0x22

static esp_err_t i2c_write(i2c_port_t i2c_num, uint8_t *data, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_ERROR_CHECK(i2c_master_start(cmd));
    esp_err_t err = i2c_master_write_byte(
        cmd,
        ES_ADDRESS,
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
        1000 / portTICK_RATE_MS
    );
    i2c_cmd_link_delete(cmd);
    return ret;
}

static esp_err_t es_write_reg(uint8_t reg, uint8_t data)
{
    uint8_t out[2];
    out[0] = reg;
    out[1] = data;

    esp_err_t err = i2c_write(I2C_NUM_0, out, 2);
    return err;
}

void es_i2c_init()
{
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(I2C_NUM_0, &conf);
    esp_err_t err = i2c_driver_install(I2C_NUM_0, conf.mode,
        I2C_MASTER_RX_BUF_DISABLE, I2C_MASTER_TX_BUF_DISABLE, 0);
    ESP_ERROR_CHECK(err);

    //Set to Slave Mode
    err = es_write_reg(REG_MASTER_MODE_CONTROL, 0x00);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Power down DEM and STM
    err = es_write_reg(REG_CHIP_PWR_MANAGEMENT, 0xF3);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set same LRCK
    err = es_write_reg(REG_DAC_CONTROL_21, 0x80);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    
    //Set chip to Play and Record Mode
    err = es_write_reg(REG_CHIP_CONTROL_1, 0x05);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Power up analog and bias
    err = es_write_reg(REG_CHIP_CONTROL_2, 0x40);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Power up ADC
    err = es_write_reg(REG_ADC_PWR_MANAGEMENT, 0b10100000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Power up DAC and enable LROUT/ROUT
    err = es_write_reg(REG_DAC_PWR_MANAGEMENT, 0b00110000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Select Analog input channel for ADC
    err = es_write_reg(REG_ADC_CONTROL_2, 0b01010000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set Analog Input PGA Gain
    err = es_write_reg(REG_ADC_CONTROL_1, 0b00000100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set SFI for ADC
    err = es_write_reg(REG_ADC_CONTROL_4, 0b00001100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set MCLK/LRCK ratio for ADC
    err = es_write_reg(REG_ADC_CONTROL_5, 0x02);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set ADC Digital Volume
    err = es_write_reg(REG_ADC_CONTROL_8, 0x0);
    err += es_write_reg(REG_ADC_CONTROL_9, 0x0);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set SFI for DAC
    err = es_write_reg(REG_DAC_CONTROL_1, 0b00011000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set MCLK/LRCK ratio for DAC
    err = es_write_reg(REG_DAC_CONTROL_2, 0x02);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set DAC Digital Volume
    err = es_write_reg(REG_DAC_CONTROL_4, 0x00);
    err += es_write_reg(REG_DAC_CONTROL_5, 0x00);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Setup Mixer
    err = es_write_reg(REG_DAC_CONTROL_16, 0b00000011);
    err += es_write_reg(REG_DAC_CONTROL_17, 0b10111000); //1011 1000
    err += es_write_reg(REG_DAC_CONTROL_18, 0x38); //0011 1000
    err += es_write_reg(REG_DAC_CONTROL_19, 0x38); //0011 1000
    err += es_write_reg(REG_DAC_CONTROL_20, 0b10010000);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Set LOUT/ROUT Volume
    err = es_write_reg(REG_DAC_CONTROL_24,  0b00010100);
    err += es_write_reg(REG_DAC_CONTROL_25, 0b00010100);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);

    //Power up DEM and STM
    err = es_write_reg(REG_CHIP_PWR_MANAGEMENT, 0x00);
    ESP_ERROR_CHECK_WITHOUT_ABORT(err);
}