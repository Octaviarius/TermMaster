# Paths
YAML_CPP_BUILD_DIR = $$OUT_PWD/build/yaml-cpp
YAML_CPP_INSTALL_DIR = $$OUT_PWD/install/yaml-cpp
YAML_CPP_SOURCE_DIR = $$PWD/../3dparty/yaml-cpp

# CMAKE commands
CMAKE_EXTRA_DEFS = -DCMAKE_INSTALL_PREFIX=$$YAML_CPP_INSTALL_DIR \
    -DCMAKE_GENERATOR=Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH=$$QTDIR \
    -DCMAKE_C_COMPILER=$$QMAKE_CC \
    -DCMAKE_CXX_COMPILER=$$QMAKE_CXX \
    -DYAML_BUILD_SHARED_LIBS=ON

build_yaml_cpp.target = build_yaml_cpp
win32 {
    build_yaml_cpp.commands += $(CHK_DIR_EXISTS) $$shell_path($$YAML_CPP_BUILD_DIR) ($(MKDIR) $$shell_path($$YAML_CPP_BUILD_DIR)) else (echo "")
}
unix {
    build_yaml_cpp.commands += $(MKDIR) -p $$shell_path($$YAML_CPP_BUILD_DIR)
}
build_yaml_cpp.commands += && cd $$shell_path($$YAML_CPP_BUILD_DIR)
build_yaml_cpp.commands += && cmake $$YAML_CPP_SOURCE_DIR $$CMAKE_EXTRA_DEFS
build_yaml_cpp.commands += && cmake --build . --config Release --target install

# Target config
QMAKE_EXTRA_TARGETS += build_yaml_cpp
PRE_TARGETDEPS += build_yaml_cpp

INCLUDEPATH += $$YAML_CPP_INSTALL_DIR/include
LIBS += -L$$YAML_CPP_INSTALL_DIR/lib -L$$YAML_CPP_INSTALL_DIR/bin -lyaml-cpp
