Import("env")

import os


def build_lvgl_demo_widgets() -> None:
    libdeps_dir = env.subst("$PROJECT_LIBDEPS_DIR")
    pioenv = env.subst("$PIOENV")
    lvgl_dir = os.path.join(libdeps_dir, pioenv, "lvgl")

    widgets_dir = os.path.join(lvgl_dir, "demos", "widgets")
    assets_dir = os.path.join(widgets_dir, "assets")

    if not os.path.isdir(widgets_dir):
        print(f"[lvgl demos] Not found: {widgets_dir}")
        return

    build_dir = env.subst("$BUILD_DIR")
    # BuildSources is recursive; building `assets_dir` separately would compile the same .c files twice.
    env.BuildSources(os.path.join(build_dir, "lvgl_demo_widgets"), widgets_dir)
    _ = assets_dir


build_lvgl_demo_widgets()
