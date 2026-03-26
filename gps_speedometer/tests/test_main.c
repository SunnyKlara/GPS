/*============================================================================
 * 测试入口
 *============================================================================*/

#include <stdio.h>

/* 外部声明 */
extern int test_nmea_run(void);
extern int test_speed_run(void);

/* HAL初��化（测试环境需要） */
extern void hal_flash_init(void);
extern void hal_system_init(void);

int main(void)
{
    printf("========================================\n");
    printf("  GPS测速仪 - 单元测试\n");
    printf("========================================\n");

    hal_system_init();
    hal_flash_init();

    int total_fail = 0;
    total_fail += test_nmea_run();
    total_fail += test_speed_run();

    printf("\n========================================\n");
    if (total_fail == 0) {
        printf("  所有测试通过!\n");
    } else {
        printf("  %d 个测试失��!\n", total_fail);
    }
    printf("========================================\n");

    return total_fail;
}
