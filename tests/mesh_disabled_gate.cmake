if(NOT CHRONON3D_ENABLE_MESH)
    chronon3d_add_test_suite(
        NAME   chronon3d_mesh_disabled_smoke
        TIER   UNIT
        SOURCES
            assets/mesh_disabled_smoke.cpp
    )
endif()
