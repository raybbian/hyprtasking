project('hyprtasking', 'cpp',
  version: '0.1',
  default_options: ['buildtype=debug'],
  meson_version: '>= 1.1'
)

cpp_compiler = meson.get_compiler('cpp')
if cpp_compiler.has_argument('-std=c++23')
  add_global_arguments('-std=c++23', language: 'cpp')
elif cpp_compiler.has_argument('-std=c++2b')
  add_global_arguments('-std=c++2b', language: 'cpp')
else
  error('Could not configure current C++ compiler (' + cpp_compiler.get_id() + ' ' + cpp_compiler.version() + ') with required C++ standard (C++23)')
endif

add_project_arguments(
  [
    '-Wno-narrowing', '-shared', '-fPIC', '--no-gnu-unique'
  ],
  language: 'cpp')

globber = run_command('find', '.', '-path', './build', '-prune', '-o', '-name', '*.cpp', '-print', check: true)
src = globber.stdout().strip().split('\n')

incdir = []
dependencies = [
  dependency('pixman-1'),
  dependency('libdrm'),
  dependency('pangocairo'),
  dependency('libinput'),
  dependency('libudev'),
  dependency('wayland-server'),
  dependency('xkbcommon'),
]

if get_option('local_include')
  incdir += include_directories('include')
  incdir += include_directories('include/hyprland/protocols')
else
  hyprland_dep = dependency('hyprland')
  dependencies += hyprland_dep

  # The Hyprland pkg-config headers can retain the dependency versions from
  # the build that produced them.  The plugin ABI hash, however, must match
  # the versions used by the running compositor.  Keep the version macros in
  # PluginAPI.hpp aligned with the dependencies used for this build.
  aquamarine_dep = dependency('aquamarine')
  hyprutils_dep = dependency('hyprutils')
  hyprcursor_dep = dependency('hyprcursor')
  hyprgraphics_dep = dependency('hyprgraphics')
  hyprlang_dep = dependency('hyprlang')

  aquamarine_version = aquamarine_dep.version().split('.')
  compat_config = configuration_data()
  compat_config.set('AQUAMARINE_VERSION', aquamarine_dep.version())
  compat_config.set('AQUAMARINE_VERSION_MAJOR', aquamarine_version[0])
  compat_config.set('AQUAMARINE_VERSION_MINOR', aquamarine_version[1])
  compat_config.set('AQUAMARINE_VERSION_PATCH', aquamarine_version[2])
  compat_config.set('HYPRLANG_VERSION', hyprlang_dep.version())
  compat_config.set('HYPRUTILS_VERSION', hyprutils_dep.version())
  compat_config.set('HYPRCURSOR_VERSION', hyprcursor_dep.version())
  compat_config.set('HYPRGRAPHICS_VERSION', hyprgraphics_dep.version())

  compat_header = configure_file(
    input: 'src/hyprland-version-compat.hpp.in',
    output: 'hyprland-version-compat.hpp',
    configuration: compat_config
  )
  add_project_arguments(
    '-include',
    join_paths(meson.current_build_dir(), 'hyprland-version-compat.hpp'),
    language: 'cpp'
  )
endif

shared_module(meson.project_name(), src,
  dependencies: dependencies,
  install: true,
  include_directories: incdir
)
