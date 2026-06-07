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

    std::memcpy(&prev_in_report_, in_report, sizeof(DInput::InReport));

    // Запрашиваем VID и PID текущего подключенного устройства через TinyUSB
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;

    // ОКОНЧАТЕЛЬНЫЙ ИСПРАВЛЕННЫЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION
    if (vid == 0x0583 && pid == 0xA009)
    {
        if (len < 6)
        {
            tuh_hid_receive_report(address, instance);
            return;
        }

        // 1. Считываем оси стиков напрямую из первых 4 байт (Байты 0, 1, 2, 3)
        uint8_t raw_lx = report[0]; 
        uint8_t raw_ly = report[1]; 
        uint8_t raw_rx = report[2]; 
        uint8_t raw_ry = report[3]; 

        // Масштабируем оси в int16_t (-32768..32767) с инверсией Y для Xbox стандарта
        gp_in.joystick_lx = (static_cast<int16_t>(raw_lx) - 128) * 256;
        gp_in.joystick_ly = (static_cast<int16_t>(raw_ly) - 128) * -256;
        gp_in.joystick_rx = (static_cast<int16_t>(raw_rx) - 128) * 256;
        gp_in.joystick_ry = (static_cast<int16_t>(raw_ry) - 128) * -256;

        // 2. Читаем байты кнопок напрямую с аппаратной инверсией (~)
        uint8_t b1 = ~report[4]; // Основные физические кнопки
        uint8_t b2 = ~report[5]; // Сервисные кнопки и Hat

        // ИСПРАВЛЕНИЕ РОКИРОВКИ КНОПОК (Перенаправляем биты обратно на свои места)
        
        // Физические кнопки 1 и 2 (были перепутаны с битами Select/Start)
        if (b2 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // Бит 0x01 из b2 отправляем на кнопку A (B0)
        if (b2 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // Бит 0x02 из b2 отправляем на кнопку B (B1)

        // Физические кнопки Select и Start (теперь читаются из b1, где раньше была путаница)
        if (b1 & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // Отправляем на системный Back (Select)
        if (b1 & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_START; // Отправляем на системный Start

        // Распределяем остальные абсолютно верные физические кнопки геймпада
        if (b1 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // Кнопка 3 -> X
        if (b1 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // Кнопка 4 -> Y
        if (b1 & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // Кнопка 5 -> LB
        if (b1 & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // Кнопка 6 -> RB
        
        // Нижние курки как триггеры Xbox (LT/RT)
        gp_in.trigger_l = (b1 & 0x40) ? 255 : 0;                  // Кнопка 7 -> LT
        gp_in.trigger_r = (b1 & 0x80) ? 255 : 0;                  // Кнопка 8 -> RT

        // Нажатия на сами грибки (L3 / R3) из второго байта кнопок
        if (b2 & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // Кнопка 11 -> L3
        if (b2 & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // Кнопка 12 -> R3

        // 3. Разбор крестовины (D-Pad Hat 0). Направления берем чистыми из верхнего полубайта b2
        uint8_t hat = report[5] >> 4;

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
}

bool DInputHost::send_feedback(Gamepad& gamepad, uint8_t address, uint8_t instance)
{
    return true;
}
