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

    // Запрашиваем VID и PID текущего подключенного устройства через TinyUSB
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;   

    // ИДЕАЛЬНЫЙ НАТИВНЫЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION (ПО РЕЗУЛЬТАТАМ ЗАХВАТА)
    if (vid == 0x0583 && pid == 0xA009)
    {
        // 1. Парсим оси аналоговых стиков на основе лога (Байты 0, 1, 2, 3)
        // Диапазон dinput 0..255 переводим в int16_t для Xbox, инвертируя оси Y (вверх = плюс)
        uint8_t raw_lx = (len > 0) ? report[0] : 128;
        uint8_t raw_ly = (len > 1) ? report[1] : 128;
        uint8_t raw_rx = (len > 2) ? report[2] : 128;
        uint8_t raw_ry = (len > 3) ? report[3] : 128;

        gp_in.joystick_lx = (static_cast<int16_t>(raw_lx) - 128) * 256;
        gp_in.joystick_ly = (static_cast<int16_t>(raw_ly) - 128) * -256;
        gp_in.joystick_rx = (static_cast<int16_t>(raw_rx) - 128) * 256;
        gp_in.joystick_ry = (static_cast<int16_t>(raw_ry) - 128) * -256;

        // 2. Парсим блок кнопок на основе лога (У Genius они идут в байтах 4 и 5)
        uint8_t b1 = (len > 4) ? report[4] : 0;
        uint8_t b2 = (len > 5) ? report[5] : 0;

        // Распределяем биты кнопок (индексы сдвинуты на 1 относительно лога pygame)
        if (b1 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // Кнопка 1 (face_1)
        if (b1 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // Кнопка 2 (face_2)
        if (b1 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // Кнопка 3 (face_3)
        if (b1 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // Кнопка 4 (face_4)
        if (b1 & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // Кнопка 5 (shoulder_l)
        if (b1 & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // Кнопка 6 (shoulder_r)
        
        // Аналоговые курки Xbox (Trigger L/R) вешаем на нижние курки Genius (Кнопки 7 и 8)
        if (b1 & 0x40) gp_in.trigger_l = Range::MAX<uint8_t>;     // Кнопка 7 (trigger_l)
        if (b1 & 0x80) gp_in.trigger_r = Range::MAX<uint8_t>;     // Кнопка 8 (trigger_r)

        // Сервисные кнопки из второго байта
        if (b2 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // Кнопка 9 (select)
        if (b2 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_START; // Кнопка 10 (start)
        if (b2 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // Кнопка 11 (stick_l3)
        if (b2 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // Кнопка 12 (stick_r3)

        // 3. Парсим крестовину (D-Pad) на основе данных о Hat 0
        // У Genius состояние Hat-switch кодируется в байте 4 (обычно верхние 4 бита) или в байте 5
        // Защитим чтение: проверяем стандартные dinput-смещения Hat-переключателя
        uint8_t hat = (len > 4) ? (report[4] >> 4) : 0x0F; 
        if (hat > 7) hat = b2 >> 4; // Проверка альтернативного смещения для старых ревизий

        switch (hat)
        {
            case 0: gp_in.dpad |= gamepad.MAP_DPAD_UP; break;
            case 1: gp_in.dpad |= gamepad.MAP_DPAD_UP | gamepad.MAP_DPAD_RIGHT; break;
            case 2: gp_in.dpad |= gamepad.MAP_DPAD_RIGHT; break;
            case 3: gp_in.dpad |= gamepad.MAP_DPAD_DOWN | gamepad.MAP_DPAD_RIGHT; break;
            case 4: gp_in.dpad |= gamepad.MAP_DPAD_DOWN; break;
            case 5: gp_in.dpad |= gamepad.MAP_DPAD_DOWN | gamepad.MAP_DPAD_LEFT; break;
            case 6: gp_in.dpad |= gamepad.MAP_DPAD_LEFT; break;
            case 7: gp_in.dpad |= gamepad.MAP_DPAD_UP | gamepad.MAP_DPAD_LEFT; break;
            default: break;
        }
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

        if (hid_joystick_data_.buttons[1])  gp_in.buttons |= gamepad.MAP_BUTTON_X;
        if (hid_joystick_data_.buttons[2])  gp_in.buttons |= gamepad.MAP_BUTTON_A;
        if (hid_joystick_data_.buttons[3])  gp_in.buttons |= gamepad.MAP_BUTTON_B;
        if (hid_joystick_data_.buttons[4])  gp_in.buttons |= gamepad.MAP_BUTTON_Y;
        if (hid_joystick_data_.buttons[5])  gp_in.buttons |= gamepad.MAP_BUTTON_LB;
        if (hid_joystick_data_.buttons[6])  gp_in.buttons |= gamepad.MAP_BUTTON_RB;
        if (hid_joystick_data_.buttons[7])  gp_in.trigger_l = Range::MAX<uint8_t>;
        if (hid_joystick_data_.buttons[8])  gp_in.trigger_r = Range::MAX<uint8_t>;
        if (hid_joystick_data_.buttons[9])  gp_in.buttons |= gamepad.MAP_BUTTON_BACK;
        if (hid_joystick_data_.buttons[10]) gp_in.buttons |= gamepad.MAP_BUTTON_START;
        if (hid_joystick_data_.buttons[11]) gp_in.buttons |= gamepad.MAP_BUTTON_L3;
        if (hid_joystick_data_.buttons[12]) gp_in.buttons |= gamepad.MAP_BUTTON_R3;
        if (hid_joystick_data_.buttons[13]) gp_in.buttons |= gamepad.MAP_BUTTON_SYS;
        if (hid_joystick_data_.buttons[14]) gp_in.buttons |= gamepad.MAP_BUTTON_MISC;
    }

    gamepad.set_pad_in(gp_in);

    tuh_hid_receive_report(address, instance);
}

bool HIDHost::send_feedback(Gamepad& gamepad, uint8_t address, uint8_t instance)
{
    return true;
}
