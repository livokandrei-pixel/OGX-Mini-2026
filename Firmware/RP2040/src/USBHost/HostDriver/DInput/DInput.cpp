#include <cstring>

#include "host/usbh.h"
#include "class/hid/hid_host.h"

#include "USBHost/HostDriver/DInput/DInput.h"

void DInputHost::initialize(Gamepad& gamepad, uint8_t address, uint8_t instance, const uint8_t* report_desc, uint16_t desc_len) 
{
    gamepad.set_analog_host(true);
    tuh_hid_receive_report(address, instance);
}

void DInputHost::process_report(Gamepad& gamepad, uint8_t address, uint8_t instance, const uint8_t* report, uint16_t len)
{
    const DInput::InReport* in_report = reinterpret_cast<const DInput::InReport*>(report);
    if (std::memcmp(&prev_in_report_, in_report, sizeof(DInput::InReport)) == 0)
    {
        tuh_hid_receive_report(address, instance);
        return;
    }

    // Запрашиваем VID и PID текущего подключенного устройства через TinyUSB
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;

    // ПЕРСОНАЛЬНЫЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION
    if (vid == 0x0583 && pid == 0xA009)
    {
        // Безопасная проверка длины пакета данных устройства
        if (len < 6)
        {
            tuh_hid_receive_report(address, instance);
            return;
        }

        // 1. Извлекаем оси стиков по карте из вашего лога захвата SDL (Байты 0, 1, 2, 3)
        uint8_t raw_lx = report[0]; // axis 0 (axis_lstick)
        uint8_t raw_ly = report[1]; // axis 1 (axis_lstick)
        uint8_t raw_rx = report[2]; // axis 2 (axis_rstick)
        uint8_t raw_ry = report[3]; // axis 3 (axis_rstick)

        // Масштабируем оси в int16_t (-32768..32767) с инверсией Y-направлений для Xbox стандарта
        gp_in.joystick_lx = (static_cast<int16_t>(raw_lx) - 128) * 256;
        gp_in.joystick_ly = (static_cast<int16_t>(raw_ly) - 128) * -256;
        gp_in.joystick_rx = (static_cast<int16_t>(raw_rx) - 128) * 256;
        gp_in.joystick_ry = (static_cast<int16_t>(raw_ry) - 128) * -256;

        // 2. Извлекаем кнопки напрямую из сырого пакета (Байты 4 и 5)
        uint8_t b1 = report[4]; // Кнопки 1-8 (в pygame это индексы 0-7)
        uint8_t b2 = report[5]; // Кнопки 9-12 (в pygame это индексы 8-11) и Hat-переключатель

        // Побитовый маппинг кнопок 1 в 1 под раскладку оригинального Xbox
        if (b1 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // button 0 (face_1) -> Кнопка 1
        if (b1 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // button 1 (face_2) -> Кнопка 2
        if (b1 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // button 2 (face_3) -> Кнопка 3
        if (b1 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // button 3 (face_4) -> Кнопка 4
        if (b1 & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // button 4 (shoulder_l) -> Кнопка 5
        if (b1 & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // button 5 (shoulder_r) -> Кнопка 6

        // Цифровые курки геймпада (Кнопки 7 и 8) выводим как полноценные триггеры Xbox
        gp_in.trigger_l = (b1 & 0x40) ? 255 : 0; // button 6 (trigger_l) -> Кнопка 7
        gp_in.trigger_r = (b1 & 0x80) ? 255 : 0; // button 7 (trigger_r) -> Кнопка 8

        // Сервисные кнопки из второго байта кнопок
        if (b2 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // button 8 (select) -> Кнопка 9
        if (b2 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_START; // button 9 (start) -> Кнопка 10
        if (b2 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // button 10 (stick_l3) -> Кнопка 11
        if (b2 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // button 11 (stick_r3) -> Кнопка 12

        // 3. Точный разбор крестовины (D-Pad Hat 0) из верхнего полубайта b2 (смещение на 4 бита)
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
            default: break; // Значения 8..15 означают состояние покоя крестовины
        }
    }
    else
    {
        // ОРИГИНАЛЬНЫЙ СТОКОВЫЙ ПАРСЕР ДЛЯ ВСЕХ ОСТАЛЬНЫХ ГЕЙМПАДОВ
        switch (in_report->dpad & DInput::DPAD_MASK)
        {
            case DInput::DPad::UP:          gp_in.dpad |= gamepad.MAP_DPAD_UP; break;
            case DInput::DPad::DOWN:        gp_in.dpad |= gamepad.MAP_DPAD_DOWN; break;
            case DInput::DPad::LEFT:        gp_in.dpad |= gamepad.MAP_DPAD_LEFT; break;
            case DInput::DPad::RIGHT:       gp_in.dpad |= gamepad.MAP_DPAD_RIGHT; break;
            case DInput::DPad::UP_RIGHT:    gp_in.dpad |= gamepad.MAP_DPAD_UP_RIGHT; break;
            case DInput::DPad::DOWN_RIGHT:  gp_in.dpad |= gamepad.MAP_DPAD_DOWN_RIGHT; break;
            case DInput::DPad::DOWN_LEFT:   gp_in.dpad |= gamepad.MAP_DPAD_DOWN_LEFT; break;
            case DInput::DPad::UP_LEFT:     gp_in.dpad |= gamepad.MAP_DPAD_UP_LEFT; break;
            default: break;
        }

        if (in_report->buttons[0] & DInput::Buttons0::SQUARE)   gp_in.buttons |= gamepad.MAP_BUTTON_X;
        if (in_report->buttons[0] & DInput::Buttons0::CROSS)    gp_in.buttons |= gamepad.MAP_BUTTON_A;
        if (in_report->buttons[0] & DInput::Buttons0::CIRCLE)   gp_in.buttons |= gamepad.MAP_BUTTON_B;
        if (in_report->buttons[0] & DInput::Buttons0::TRIANGLE) gp_in.buttons |= gamepad.MAP_BUTTON_Y;
        if (in_report->buttons[0] & DInput::Buttons0::L1)       gp_in.buttons |= gamepad.MAP_BUTTON_LB;
        if (in_report->buttons[0] & DInput::Buttons0::R1)       gp_in.buttons |= gamepad.MAP_BUTTON_RB;
        if (in_report->buttons[1] & DInput::Buttons1::L3)       gp_in.buttons |= gamepad.MAP_BUTTON_L3;
        if (in_report->buttons[1] & DInput::Buttons1::R3)       gp_in.buttons |= gamepad.MAP_BUTTON_R3; 
        if (in_report->buttons[1] & DInput::Buttons1::SELECT)   gp_in.buttons |= gamepad.MAP_BUTTON_BACK;
        if (in_report->buttons[1] & DInput::Buttons1::START)    gp_in.buttons |= gamepad.MAP_BUTTON_START;
        if (in_report->buttons[1] & DInput::Buttons1::SYS)      gp_in.buttons |= gamepad.MAP_BUTTON_SYS;
        if (in_report->buttons[1] & DInput::Buttons1::TP)       gp_in.buttons |= gamepad.MAP_BUTTON_MISC;

        if (gamepad.analog_enabled())
        {
            gp_in.analog[gamepad.MAP_ANALOG_OFF_UP]    = in_report->up_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_DOWN]  = in_report->down_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_LEFT]  = in_report->left_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_RIGHT] = in_report->right_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_A]  = in_report->cross_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_B]  = in_report->circle_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_X]  = in_report->square_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_Y]  = in_report->triangle_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_LB] = in_report->l1_axis;
            gp_in.analog[gamepad.MAP_ANALOG_OFF_RB] = in_report->r1_axis;
        }

        if (in_report->l2_axis > 0)
        {
            gp_in.trigger_l = gamepad.scale_trigger_l(in_report->l2_axis);
        }
        else
        {
            gp_in.trigger_l = (in_report->buttons[0] & DInput::Buttons0::L2) ? Range::MAX<uint8_t> : Range::MIN<uint8_t>;
        }
        if (in_report->r2_axis > 0)
        {
            gp_in.trigger_r = gamepad.scale_trigger_r(in_report->r2_axis);
        }
        else
        {
            gp_in.trigger_r = (in_report->buttons[0] & DInput::Buttons0::R2) ? Range::MAX<uint8_t> : Range::MIN<uint8_t>;
        }

        std::tie(gp_in.joystick_lx, gp_in.joystick_ly) = gamepad.scale_joystick_l(in_report->joystick_lx, in_report->joystick_ly);
        std::tie(gp_in.joystick_rx, gp_in.joystick_ry) = gamepad.scale_joystick_r(in_report->joystick_rx, in_report->joystick_ry);
    }

    gamepad.set_pad_in(gp_in);

    tuh_hid_receive_report(address, instance);
    std::memcpy(&prev_in_report_, in_report, sizeof(DInput::InReport));
}

bool DInputHost::send_feedback(Gamepad& gamepad, uint8_t address, uint8_t instance)
{
    return true;
}
