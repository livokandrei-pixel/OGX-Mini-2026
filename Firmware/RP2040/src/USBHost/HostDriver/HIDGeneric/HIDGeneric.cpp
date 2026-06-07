#include <cstring>
#include <memory>

#include "host/usbh.h"
#include "class/hid/hid_host.h"

#include "USBHost/HIDParser/HIDReportDescriptor.h"
#include "USBHost/HostDriver/HIDGeneric/HIDGeneric.h"

void HIDHost::initialize(Gamepad& gamepad, uint8_t address, uint8_t instance, const uint8_t* report_desc, uint16_t desc_len)
{
    if (!report_desc || desc_len == 0)
    {
        return;
    }
    
    report_desc_len_ = desc_len;
    std::memcpy(report_desc_buffer_.data(), report_desc, std::min(static_cast<size_t>(report_desc_len_), report_desc_buffer_.size()));
    hid_joystick_ = std::make_unique<HIDJoystick>(std::make_shared<HIDReportDescriptor>(report_desc_buffer_.data(), report_desc_len_));

    tuh_hid_receive_report(address, instance);
}

void HIDHost::process_report(Gamepad& gamepad, uint8_t address, uint8_t instance, const uint8_t* report, uint16_t len)
{
    if (std::memcmp(prev_report_in_.data(), report, len) == 0)
    {
        tuh_hid_receive_report(address, instance);
        return;
    }

    std::memcpy(prev_report_in_.data(), report, len);

    // Безопасно запрашиваем VID и PID текущего подключенного устройства через TinyUSB
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;   

    // ПЕРСОНАЛЬНЫЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION (С АНАЛОГАМИ)
    if (vid == 0x0583 && pid == 0xA009)
    {
        // ВРЕМЕННЫЙ ЛОГ: Выводит длину пакета и первые 8 байт в шестнадцатеричном виде (HEX) в COM-порт
        printf("[Genius] len:%d -> %02X %02X %02X %02X %02X %02X %02X %02X\n", 
               len, report[0], report[1], report[2], report[3], report[4], report[5], report[6], report[7]);

        // 1. Извлекаем позиции левого аналогового стика (Байты 0 и 1, центр 128)
        uint8_t raw_lx = report[0];
        uint8_t raw_ly = report[1];
        gp_in.joystick_lx = (static_cast<int16_t>(raw_lx) - 128) * 256;
        gp_in.joystick_ly = (static_cast<int16_t>(raw_ly) - 128) * -256; // Инверсия Y под стандарт Xbox

        // 2. Извлекаем позиции правого аналогового стика (Байты 2 и 3, центр 128)
        uint8_t raw_rx = report[2];
        uint8_t raw_ry = report[3];
        gp_in.joystick_rx = (static_cast<int16_t>(raw_rx) - 128) * 256;
        gp_in.joystick_ry = (static_cast<int16_t>(raw_ry) - 128) * -256; // Инверсия Y под стандарт Xbox

        // 3. Извлекаем блоки кнопок (Обычно упакованы с 4-го байта)
        uint8_t b1 = report[4];
        uint8_t b2 = (len > 5) ? report[5] : 0;

        // Побитовое распределение физических кнопок геймпада
        if (b1 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // Кнопка 1
        if (b1 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // Кнопка 2
        if (b1 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // Кнопка 3
        if (b1 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // Кнопка 4
        if (b1 & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // Кнопка 5
        if (b1 & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // Кнопка 6
        if (b1 & 0x40) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // Кнопка 7 (Select)
        if (b1 & 0x80) gp_in.buttons |= gamepad.MAP_BUTTON_START; // Кнопка 8 (Start)

        // Кнопки нажатия на сами аналоговые грибки (L3 и R3)
        if (b2 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // Кнопка 9
        if (b2 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // Кнопка 10

        // 4. Обработка цифровой крестовины (D-Pad)
        // В режиме "Analog" (светодиод горит) крестовина кодируется как Hat-Switch в нижних битах b1
        uint8_t hat = b1 & 0x0F; 
        
        // Мапим явные положения Hat-Switch или дублируем с крайних зон левого стика для надежности
        if (hat == 0x00 || raw_lx < 40)  gp_in.dpad |= gamepad.MAP_DPAD_LEFT;
        if (hat == 0x02 || raw_lx > 215) gp_in.dpad |= gamepad.MAP_DPAD_RIGHT;
        if (hat == 0x01 || raw_ly < 40)  gp_in.dpad |= gamepad.MAP_DPAD_UP;
        if (hat == 0x03 || raw_ly > 215) gp_in.dpad |= gamepad.MAP_DPAD_DOWN;
    }
    else
    {
        // ОРИГИНАЛЬНЫЙ СТОКОВЫЙ ПАРСЕР ДЛЯ ВСЕХ ОСТАЛЬНЫХ ГЕЙМПАДОВ
        if (!hid_joystick_->parseData(const_cast<uint8_t*>(report), len, &hid_joystick_data_))
        {
            tuh_hid_receive_report(address, instance);
            return;
        }

        switch (hid_joystick_data_.hat_switch)
        {
            case HIDJoystickHatSwitch::UP:
                gp_in.dpad |= gamepad.MAP_DPAD_UP;
                break;
            case HIDJoystickHatSwitch::UP_RIGHT:
                gp_in.dpad |= gamepad.MAP_DPAD_UP_RIGHT;
                break;
            case HIDJoystickHatSwitch::RIGHT:
                gp_in.dpad |= gamepad.MAP_DPAD_RIGHT;
                break;
            case HIDJoystickHatSwitch::DOWN_RIGHT:
                gp_in.dpad |= gamepad.MAP_DPAD_DOWN_RIGHT;
                break;
            case HIDJoystickHatSwitch::DOWN:
                gp_in.dpad |= gamepad.MAP_DPAD_DOWN;
                break;
            case HIDJoystickHatSwitch::DOWN_LEFT:
                gp_in.dpad |= gamepad.MAP_DPAD_DOWN_LEFT;
                break;
            case HIDJoystickHatSwitch::LEFT:
                gp_in.dpad |= gamepad.MAP_DPAD_LEFT;
                break;
            case HIDJoystickHatSwitch::UP_LEFT:
                gp_in.dpad |= gamepad.MAP_DPAD_UP_LEFT;
                break;
            default:
                break;
        }

        std::tie(gp_in.joystick_lx, gp_in.joystick_ly) = gamepad.scale_joystick_l(hid_joystick_data_.X, hid_joystick_data_.Y);
        std::tie(gp_in.joystick_rx, gp_in.joystick_ry) = gamepad.scale_joystick_r(hid_joystick_data_.Z, hid_joystick_data_.Rz);

        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_X;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_A;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_B;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_Y;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_LB;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_RB;
        if (hid_joystick_data_.buttons)  gp_in.trigger_l = Range::MAX<uint8_t>;
        if (hid_joystick_data_.buttons)  gp_in.trigger_r = Range::MAX<uint8_t>;
        if (hid_joystick_data_.buttons)  gp_in.buttons |= gamepad.MAP_BUTTON_BACK;
        if (hid_joystick_data_.buttons) gp_in.buttons |= gamepad.MAP_BUTTON_START;
        if (hid_joystick_data_.buttons) gp_in.buttons |= gamepad.MAP_BUTTON_L3;
        if (hid_joystick_data_.buttons) gp_in.buttons |= gamepad.MAP_BUTTON_R3;
        if (hid_joystick_data_.buttons) gp_in.buttons |= gamepad.MAP_BUTTON_SYS;
        if (hid_joystick_data_.buttons) gp_in.buttons |= gamepad.MAP_BUTTON_MISC;
    }

    gamepad.set_pad_in(gp_in);

    tuh_hid_receive_report(address, instance);
}

bool HIDHost::send_feedback(Gamepad& gamepad, uint8_t address, uint8_t instance)
{
    return true;
}
