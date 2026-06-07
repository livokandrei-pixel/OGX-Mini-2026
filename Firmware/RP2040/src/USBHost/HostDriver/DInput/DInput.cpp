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
    // Сначала даем штатному оригинальному парсеру OGX-Mini полностью расшифровать пакет.
    // Функция parseData сама уберет инверсию, разделит Hat-switch крестовины и кнопки!
    const DInput::InReport* in_report = reinterpret_cast<const DInput::InReport*>(report);
    if (std::memcmp(&prev_in_report_, in_report, sizeof(DInput::InReport)) == 0)
    {
        tuh_hid_receive_report(address, instance);
        return;
    }

    std::memcpy(&prev_in_report_, in_report, sizeof(DInput::InReport));

    // Узнаем VID/PID устройства
    uint16_t vid = 0;
    uint16_t pid = 0;
    tuh_vid_pid_get(address, &vid, &pid);

    Gamepad::PadIn gp_in;

    // ПЕРСОНАЛЬНЫЙ ДРАЙВЕР ДЛЯ GENIUS MAXFIRE G-12U VIBRATION (БЕЗОПАСНЫЙ МЕТОД)
    if (vid == 0x0583 && pid == 0xA009)
    {
        // 1. Извлекаем крестовину (D-Pad), которую парсер уже очистил от кнопок
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

        // 2. Обрабатываем СТИКИ. Так как у Genius оси идут стандартно (X, Y, Z, Rz),
        // мы используем родной метод масштабирования, и они встанут ровно по центру!
        std::tie(gp_in.joystick_lx, gp_in.joystick_ly) = gamepad.scale_joystick_l(in_report->joystick_lx, in_report->joystick_ly);
        std::tie(gp_in.joystick_rx, gp_in.joystick_ry) = gamepad.scale_joystick_r(in_report->joystick_rx, in_report->joystick_ry);

        // 3. ПЕРЕНАЗНАЧАЕМ КНОПКИ. Так как они были перепутаны, мы берем оригинальный массив 
        // in_report->buttons и перенаправляем биты строго по вашей таблице SDL:
        
        // Мапим кнопки b0 (индексы кнопок 1-8)
        if (in_report->buttons[0] & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_A;     // Физическая 1 -> A
        if (in_report->buttons[0] & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_B;     // Физическая 2 -> B
        if (in_report->buttons[0] & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_X;     // Физическая 3 -> X
        if (in_report->buttons[0] & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_Y;     // Физическая 4 -> Y
        if (in_report->buttons[0] & 0x10) gp_in.buttons |= gamepad.MAP_BUTTON_LB;    // Физическая 5 -> LB
        if (in_report->buttons[0] & 0x20) gp_in.buttons |= gamepad.MAP_BUTTON_RB;    // Физическая 6 -> RB
        
        // Цифровые курки (7 и 8) выводим как триггеры Xbox (LT/RT)
        gp_in.trigger_l = (in_report->buttons[0] & 0x40) ? 255 : 0;                  // Физическая 7 -> LT
        gp_in.trigger_r = (in_report->buttons[0] & 0x80) ? 255 : 0;                  // Физическая 8 -> RT

        // Мапим кнопки b1 (индексы кнопок 9-12)
        if (in_report->buttons[1] & 0x01) gp_in.buttons |= gamepad.MAP_BUTTON_BACK;  // Физическая 9 -> Back
        if (in_report->buttons[1] & 0x02) gp_in.buttons |= gamepad.MAP_BUTTON_START; // Физическая 10 -> Start
        if (in_report->buttons[1] & 0x04) gp_in.buttons |= gamepad.MAP_BUTTON_L3;    // Физическая 11 -> L3
        if (in_report->buttons[1] & 0x08) gp_in.buttons |= gamepad.MAP_BUTTON_R3;    // Физическая 12 -> R3
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
