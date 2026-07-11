add_library(rule STATIC
    Rule.h
    Rule.cpp
    Measure.h
    Measure.cpp
    SyntheticFixture.h
    SyntheticFixture.cpp
    Selector.h
    Selector.cpp
    RuleEngine.h
    RuleEngine.cpp
    BuiltInCatalog.h
    BuiltInCatalog.cpp
)

target_link_libraries(rule PUBLIC geometry)
target_include_directories(rule PUBLIC ${CMAKE_SOURCE_DIR}/src)
