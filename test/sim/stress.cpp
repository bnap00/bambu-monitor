// Fast-knob stress test: hammers uiOnKnob with mixed timing under ASan.
#define main sim_main_unused
#include "sim_main.cpp"
#undef main
#include <random>
int main()
{
    settings.printers[0].host = "192.168.1.50";
    settings.printers[1].host = "192.168.1.51";
    setupFakes();
    lv_init();
    static lv_disp_draw_buf_t db;
    static lv_color_t buf[LCD_WIDTH * 98];
    lv_disp_draw_buf_init(&db, buf, nullptr, LCD_WIDTH * 98);
    static lv_disp_drv_t dd;
    lv_disp_drv_init(&dd);
    dd.hor_res = LCD_WIDTH;
    dd.ver_res = LCD_HEIGHT;
    dd.flush_cb = flush;
    dd.draw_buf = &db;
    lv_disp_drv_register(&dd);
    uiInit();
    std::mt19937 rng(1);
    for (int i = 0; i < 20000; i++)
    {
        int r = rng() % 100;
        int steps = r < 45 ? 1 : r < 90 ? -1 : (int)(rng() % 7) - 3;
        uiOnKnob(steps);
        uint32_t gap = (rng() % 4 == 0) ? 150 + rng() % 300 : 5 + rng() % 40;
        for (uint32_t t = 0; t < gap; t += 5)
        {
            fakeMs += 5;
            uiLoop();
            lv_timer_handler();
        }
        if (i % 2000 == 0)
            printf("iter %d mem ok\n", i);
    }
    printf("done\n");
}
