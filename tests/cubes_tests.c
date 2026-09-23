#include "tests/test_framework.h"
#include "tests/gfx_test.h"
#include "os/os.h"
#include "gpu/gpu.h"
#include "lib/thread_context.h"
#include "cubes/cubes_types.h"

void test_cubes_settings_cooked(void) {
  TempAllocator ta = tctx_temp_allocator_begin(NULL);
  PlatformFileData f = os_read_file("cooked/cubes_settings.hza", &ta.allocator);
  test_assert_true(f.success);
  if (f.success) {
    CubesSettings *hs = (CubesSettings *)f.buffer;
    test_assert_eq(hs->cube_count, 5);
    test_assert_eqf(hs->spin_speed, 0.8f);
    test_assert_eqf(hs->tint.g, 0.55f);
  }
  tctx_temp_allocator_end(ta);
}

void test_cubes_gfx_clear(void) {
  if (!is_main_thread()) {
    return;
  }
  GfxTestState *ts = &test_ctx()->gfx;

  for (u32 frame = 0; frame < 3; frame++) {
    gpu_begin_frame();
    GpuPassDesc pass_desc = gfx_test_default_pass_desc();
    pass_desc.clear_color = (ColorF32){.r = 0.9f, .g = 0.55f, .b = 0.2f, .a = 1.0f};
    gpu_begin_pass(&pass_desc);
    gpu_end_pass();
    if (frame == 2) {
      gpu_queue_render_target_read(ts->rt, &(GpuReadRenderTargetDesc){0});
    }
    gpu_end_frame();
  }

  test_gpu_wait();
  test_assert_true(gfx_test_capture_or_compare("test_cubes_gfx_clear"));
}

void test_app_register(void) {
  extern void app_register_types(void);
  app_register_types();

  REGISTER_TEST(test_cubes_settings_cooked);
  REGISTER_GFX_TEST(test_cubes_gfx_clear);
}
