project('libobmcsession', 'cpp',
    default_options: [
        'warning_level=3',
        'werror=true',
        'cpp_std=c++2a',
    ],
    license: 'Apache-2.0',
    version: '1.0',
)
pkg = import('pkgconfig')

boost = dependency('boost', required: true)
sdbusplus_dep = dependency('sdbusplus', required: true)
pdi_dep = dependency('phosphor-dbus-interfaces', required: true)

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

install_headers('include/libobmcsession/manager.hpp', subdir: 'libobmcsession')

obmcsession = shared_library('obmcsession',
    'src/manager.cpp',
    'src/session.cpp',
    cpp_args: cpp_args,
    version : libruntime_so_version,
    dependencies: [
        boost,
        sdbusplus_dep,
        pdi_dep,
    ],
    include_directories: [
        'include'
    ],
    install: true,
)
pkg.generate(obmcsession)
