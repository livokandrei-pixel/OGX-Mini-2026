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

    // Запрашиваем VID и PID подключенного USB-устройства через TinyUSB
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;   

    // ПЕРСОНАЛЬНЫЙ ПРЯМОЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION (БЕЗ ИСКАЖЕНИЙ PARSEDATA)
    if (vid == 0x0583 && pid == 0xA009)
    {
        // Проверяем длину пакета, чтобы избежать сбоев памяти
        if (len < 6)
        {
            tuh_hid_receive_report(address, instance);
            return;
        }

        // 1. Извлекаем оси стиков НАПРЯМУЮ ИЗ USB-ПАКЕТА на основе вашего лога захвата.
        // Байты 0 и 1 - Левый стик (X, Y). Блок dinput 0..255 переводим в int16_t для Xbox, инвертируя Y.
        uint8_t raw_lx = report[0]; 
        uint8_t raw_ly = report[1]; 
        uint8_t raw_rx = report[2]; 
        uint8_t raw_ry = report[3]; 

        gp_in.joystick_lx = (static_cast<int16_t>(raw_lx) - 128) * 256;
        gp_in.joystick_ly = (static_cast<int16_t>(raw_ly) - 128) * -256;
        gp_in.joystick_rx = (static_cast<int16_t>(raw_rx) - 128) * 256;
        gp_in.joystick_ry = (static_cast<int16_t>(raw_ry) - 128) * -256;

        // 2. Читаем кнопки напрямую из байта 4 и байта 5 (в соответствии с вашим SDL GUID)
        uint8_t b1 = report[4]; // Кнопки 1-8 (в логе pygame это индексы 0-7)
        uint8_t b2 = report[5]; // Кнопки 9-12 (в логе pygame это индексы 8-11) и крестовина

        // Распределяем биты кнопок 1 в 1 по стандарту оригинального Xbox
        if (b1 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // button 0 (face_1) -> Кнопка 1
        if (b1 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // button 1 (face_2) -> Кнопка 2
        if (b1 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // button 2 (face_3) -> Кнопка 3
        if (b1 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // button 3 (face_4) -> Кнопка 4
        if (b1 & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // button 4 (shoulder_l) -> Кнопка 5
        if (b1 & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // button 5 (shoulder_r) -> Кнопка 6
        
        // Мапим нижние курки Genius как полноценные триггеры Xbox
        if (b1 & 0x40) gp_in.trigger_l = 255;                     // button 6 (trigger_l) -> Кнопка 7
        if (b1 & 0x80) gp_in.trigger_r = 255;                     // button 7 (trigger_r) -> Кнопка 8

        // Сервисные кнопки из второго байта кнопок
        if (b2 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // button 8 (select) -> Кнопка 9
        if (b2 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_START; // button 9 (start) -> Кнопка 10
        if (b2 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // button 10 (stick_l3) -> Кнопка 11
        if (b2 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // button 11 (stick_r3) -> Кнопка 12

        // 3. Извлекаем крестовину (D-Pad Hat 0) из верхнего полубайта b2 (смещение на 4 бита)
        uint8_t hat = b2 >> 4; 

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
            default: break; // Значения 8..15 означают покой крестовины
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
            case HIDJoystickHatSwitch::UP:          gp_in.dpad |= gamepad.MAP_DPAD_UP; break;
            case HIDJoystickHatSwitch::UP_RIGHT:    gp_in.dpad |= gamepad.MAP_DPAD_UP_RIGHT; break;
            case HIDJoystickHatSwitch::RIGHT:       gp_in.dpad |= gamepad.MAP_DPAD_RIGHT; break;
            case HIDJoystickHatSwitch::DOWN_RIGHT:  gp_in.dpad |= gamepad.MAP_DPAD_DOWN_RIGHT; break;
            case HIDJoystickHatSwitch::DOWN:        gp_in.dpad |= gamepad.MAP_DPAD_DOWN; break;
            case HIDJoystickHatSwitch::DOWN_LEFT:   gp_in.dpad |= gamepad.MAP_DPAD_DOWN_LEFT; break;
            case HIDJoystickHatSwitch::LEFT:        gp_in.dpad |= gamepad.MAP_DPAD_LEFT; break;
            case HIDJoystickHatSwitch::UP_LEFT:     gp_in.dpad |= gamepad.MAP_DPAD_UP_LEFT; break;
            default: break;
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
