project('libobmcsession', ['c','cpp'],
    default_options: [
        'warning_level=3',
        'werror=true',
        'cpp_std=c++2a',
    ],
    license: 'Apache-2.0',
    version: '1.0',
)
pkg = import('pkgconfig')

boost = dependency('boost', modules: ['coroutine', 'context'], required: true)
threads = dependency('threads', required: true)
sdbusplus_dep = dependency('sdbusplus', required: true)
pdi_dep = dependency('phosphor-dbus-interfaces', required: true)
pl_dep = dependency('phosphor-logging', required: true)

cpp_args = [
    '-DBOOST_SYSTEM_NO_DEPRECATED',
    '-DBOOST_ERROR_CODE_HEADER_ONLY',
    '-DBOOST_NO_RTTI',
    '-DBOOST_NO_TYPEID',
    '-DBOOST_ALL_NO_LIB',
    '-DBOOST_ASIO_DISABLE_THREADS'
]

libruntime_lt_c=1
libruntime_lt_r=0
libruntime_lt_a=0

libruntime_so_version = '@0@.@1@.@2@'.format((libruntime_lt_c - libruntime_lt_a),
                                              libruntime_lt_a,
                                              libruntime_lt_r)

install_headers(
    'include/libobmcsession/obmcsession.h',
    'include/libobmcsession/obmcsession.hpp',
    'include/libobmcsession/obmcsession_proto.h',
    'include/libobmcsession/obmcsession_proto.hpp',
    subdir: 'libobmcsession')

obmcsession = shared_library('obmcsession',
    'src/api.cpp',
    'src/manager.cpp',
    'src/session.cpp',
    cpp_args: cpp_args,
    version : libruntime_so_version,
    dependencies: [
        boost,
        threads,
        sdbusplus_dep,
        pdi_dep,
        pl_dep,
    ],
    include_directories: [
        'include',
        'src',
    ],
    install: true,
)
pkg.generate(obmcsession)

systemd = dependency('systemd')
systemd_system_unit_dir = systemd.get_pkgconfig_variable('systemdsystemunitdir')
bindir = get_option('prefix') + '/' +get_option('bindir')

conf_data = configuration_data()
conf_data.set('MESON_INSTALL_PREFIX',get_option('prefix'))

configure_file(input : 'obmcsess-sshsrv.service.in',
    output : 'obmcsess-sshsrv.service',
    install_dir: systemd_system_unit_dir,
    configuration: conf_data,
    install : true
)

executable('obmcses-sshsvc', 
    ['services/sshsvc.cpp'],
    dependencies: [boost, sdbusplus_dep, threads, pl_dep],
    include_directories: ['include'],
    link_with: [obmcsession],
    install: true,
    install_dir: bindir
)