import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
import fw

def test_new_app_copies_template(tmp_path):
    root = tmp_path
    (root / "apps" / "template").mkdir(parents=True)
    (root / "apps" / "template" / "CMakeLists.txt").write_text(
        'add_executable(template main.c)\n')
    (root / "apps" / "template" / "main.c").write_text("// template\n")

    dest = fw.new_app("blinky", repo_root=root)

    assert dest == root / "apps" / "blinky"
    assert (dest / "main.c").exists()
    # the app target must be renamed from 'template' to the new name
    assert "add_executable(blinky" in (dest / "CMakeLists.txt").read_text()

def test_new_app_rejects_existing(tmp_path):
    root = tmp_path
    (root / "apps" / "blinky").mkdir(parents=True)
    try:
        fw.new_app("blinky", repo_root=root)
        assert False, "expected FileExistsError"
    except FileExistsError:
        pass

def test_build_command_uses_target_preset():
    cmd = fw.build_command("hello_display")
    assert cmd[:3] == ["cmake", "--build", "--preset"]
    assert "target" in cmd

def test_test_command_configures_and_runs_ctest():
    cmds = fw.test_command()
    assert cmds[0][:2] == ["cmake", "--preset"]
    assert cmds[-1][0] == "ctest"
    # host-test preset must actually be used for the configure step
    assert "host-test" in cmds[0]
    # and there must be a build step that builds the host-test preset
    assert ["cmake", "--build", "--preset", "host-test"] in cmds

def test_flash_command_programs_correct_elf():
    cmd = fw.flash_command("hello_display")
    assert cmd[0] == "openocd"
    program_arg = cmd[-1]
    assert "build/apps/hello_display/hello_display.elf" in program_arg
    assert "verify" in program_arg
    assert "reset" in program_arg

def test_rtt_command_sets_up_and_serves_rtt():
    cmd = fw.rtt_command()
    assert cmd[0] == "openocd"
    assert any("rtt setup" in c for c in cmd)
    assert any("rtt server start" in c for c in cmd)

def test_main_print_dispatches_build_command(capsys):
    fw.main(["build", "--print"])
    captured = capsys.readouterr()
    assert "cmake --build --preset target --target hello_display" in captured.out
