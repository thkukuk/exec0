project(
  'exec0',
  'c',
  meson_version : '>= 0.61.0',
  default_options : [
                  'prefix=/usr',
                  'sysconfdir=/etc',
                  'localstatedir=/var',
                  'buildtype=debugoptimized',
                  'c_std=gnu11', 'warning_level=2',
                  'default_library=shared',
                  'b_pie=true',
                  'b_lto=true',
                  'warning_level=2'],
  license : ['GPL-2.0-or-later', 'LGPL-2.1-or-later'],
  version : '0.1',
)

bindir = get_option('bindir')

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set_quoted('PACKAGE', meson.project_name())

cc = meson.get_compiler('c')
inc = include_directories(['include'])

add_project_arguments(['-D_GNU_SOURCE=1',
                       '-DXTSTRINGDEFINES',
                       '-D_FORTIFY_SOURCE=2',
                       '-D_FILE_OFFSET_BITS=64',
                       '-D_TIME_BITS=64'], language : 'c')

possible_cc_flags = [
                  '-fstack-protector-strong',
                  '-funwind-tables',
                  '-fasynchronous-unwind-tables',
                  '-fstack-clash-protection',
                  '-Werror=return-type',
                  '-Wbad-function-cast',
                  '-Wcast-align',
                  '-Wcast-qual',
                  '-Wformat-security',
                  '-Winline',
                  '-Wmissing-declarations',
                  '-Wmissing-prototypes',
                  '-Wnested-externs',
                  '-Wshadow',
                  '-Wstrict-prototypes',
                  '-Wundef',
                  ]
add_project_arguments(cc.get_supported_arguments(possible_cc_flags), language : 'c')

libsystemd = dependency('libsystemd', version : '>= 257')
libeconf = dependency('libeconf')

configure_file(output: 'config.h', configuration: conf)

exec0_exe = executable('exec0',
           ['src/main.c', 'src/main_pkexec.c', 'src/main_su.c', 'src/main_sudo.c',
            'src/ptybroker.c', 'src/polkit.c', 'src/unitwatch.c', 'src/read_config.c'],
           include_directories : inc,
           dependencies : [libsystemd, libeconf],
           install : false)

# Build-tree symlinks, so the test suite below can invoke ./su, ./pkexec and ./sudo directly without
# needing 'meson install' first; this is independent of whether they get installed or not
wrapper_symlinks = []
foreach name : ['su', 'pkexec', 'sudo']
  wrapper_symlinks += custom_target(name,
             output : name,
             command : ['ln', '-sf', 'exec0', '@OUTPUT@'],
             depends : exec0_exe,
             build_by_default : true)
endforeach

if get_option('pkexec-compat')
  install_symlink('pkexec',
                  pointing_to: 'exec0',
                  install_dir: bindir)
endif
if get_option('su-compat')
  install_symlink('su',
                  pointing_to: 'exec0',
                  install_dir: bindir)
endif
if get_option('sudo-compat')
  install_symlink('sudo',
                  pointing_to: 'exec0',
                  install_dir: bindir)
endif

subdir('man')
subdir('polkit')
subdir('tests')
