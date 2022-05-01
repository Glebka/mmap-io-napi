{
    "targets": [{
        "target_name": "mmap-io",
        "cflags!": ["-fno-exceptions"],
        "cflags_cc!": ["-fno-exceptions"],
        "msvs_settings": {
            "VCCLCompilerTool": {"ExceptionHandling": 1},
        },
        "sources": ["src/mmap-io.cc"],
        "include_dirs": [
            "<!(node -p \"require('node-addon-api').include_dir\")",
        ],
        "cflags_cc": [
            "-std=c++11"
        ],
        "conditions": [
            ['OS=="mac"',
                {"xcode_settings": {
                    'GCC_ENABLE_CPP_EXCEPTIONS': "YES",
                    'OTHER_CPLUSPLUSFLAGS': ['-std=c++11', '-stdlib=libc++'],
                    'OTHER_LDFLAGS': ['-stdlib=libc++'],
                    'MACOSX_DEPLOYMENT_TARGET': '10.8'
                }}
             ]
        ]
    }]
}
