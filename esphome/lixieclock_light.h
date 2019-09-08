#include "esphome.h"
#include "Lixie.h"

class LixieLightOutput : public PollingComponent, public LightOutput
{
public:

    LixieLightOutput() : PollingComponent(1000) {}
    Lixie *lix;

    void setup() override
    {
        // This will be called by App.setup()
        this->lix = new Lixie(D4, 4);
        this->lix->begin();
        this->lix->color(0, 0, 0);
        this->lix->clear();
    }
    LightTraits get_traits() override
    {
        // return the traits this light supports
        auto traits = LightTraits();
        traits.set_supports_brightness(true);
        traits.set_supports_rgb(true);
        traits.set_supports_rgb_white_value(false);
        traits.set_supports_color_temperature(false);
        return traits;
    }

    void write_state(LightState *state) override
    {
        ESP_LOGD("Lixie", "write_state aufgerufen");
        // This will be called by the light to get a new state to be written.
        // use any of the provided current_values methods
        float red, green, blue;
        state->current_values_as_rgb(&red, &green, &blue);
        ESP_LOGD("Lixie", "r: %f, g: %f, b: %d", red, green*255.0, byte(blue*255.0));
        this->lix->color(red*255.0, green*255.0, blue*255.0);
        this->lix->show();
        // Write red, green and blue to HW
        // ...
    }

    // void write_time(int Hour, int Minute)
    void update() override
    {
        ESP_LOGD("Lixie", "update aufgerufen");
        auto time = HA_time->now();
        int Hour, Minute;
        Hour = time.hour;
        Minute = time.minute;
        // std::string time_now;
        // char buf[10];
        // time_now += "1";
        // if (Hour < 10)
        // {
        //     time_now += "0";
        // }
        // itoa(Hour, buf, 10);
        // time_now += buf;
        // time_now += ":";

        // if (Minute < 10)
        // {
        //     time_now += "0";
        // }
        // itoa(Minute, buf, 10);
        // time_now += buf;

        // time_now.copy(buf, 10);
        // this->lix->write(buf);

        this->lix->write(10000 + Hour*100 + Minute);
        ESP_LOGD("Lixie", "current number: %d", this->lix->get_number());
    }
};