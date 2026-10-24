# The Themes section's extra test sources: the theme editors' controller glue
# (app/EditorPad.cpp, which needs ImGui but not SDL) and ImGui's core, so
# Tests/EditorPadTests.cpp can run it on a headless ImGui context.
list(APPEND RETROFE_TEST_EXTRA_SOURCES
	"${IMGUI_ROOT}/imgui.cpp"
	"${IMGUI_ROOT}/imgui_draw.cpp"
	"${IMGUI_ROOT}/imgui_tables.cpp"
	"${IMGUI_ROOT}/imgui_widgets.cpp"
	"${RETROFE_DIR}/Source/Manager/Themes/app/EditorPad.cpp"
)
include_directories("${IMGUI_ROOT}")
