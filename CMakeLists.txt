cmake_minimum_required(VERSION 3.20)
project(breaklock_challenge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Możemy ustawić, który target kompilować (domyślnie klient włączony, serwer wyłączony)
option(CLIENT "Compile client executable" ON)
option(SERVER "Compile server executable" OFF)

# Wyszukiwanie systemowej biblioteki wątków (kluczowe dla asynchronicznego ASIO na Linuxie)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(WIN32)
    set(SOCKET_LIBS ws2_32 wsock32)
endif()

# Klient
if(CLIENT)
    add_executable(breaklock_client main_client.cpp)
    target_include_directories(breaklock_client PRIVATE ${CMAKE_SOURCE_DIR}/include)
    
    # Dodajemy wymuszenie Standalone z poziomu CMake, aby lintery i kompilator to widziały
    target_compile_definitions(breaklock_client PRIVATE ASIO_STANDALONE)
    
    # Linkowanie wątków systemowych
    target_link_libraries(breaklock_client PRIVATE Threads::Threads)
    
    if(WIN32)
        target_link_libraries(breaklock_client PRIVATE ${SOCKET_LIBS})
    endif()
endif()

# Serwer
if(SERVER)
    add_executable(breaklock_server main_server.cpp)
    target_include_directories(breaklock_server PRIVATE ${CMAKE_SOURCE_DIR}/include)
    
    # Przekazujemy flagę Standalone, ponieważ kod C++ serwera jej nie posiada
    target_compile_definitions(breaklock_server PRIVATE ASIO_STANDALONE)
    
    # Linkowanie wątków systemowych (konieczne ze względu na std::atomic i asio::async_wait)
    target_link_libraries(breaklock_server PRIVATE Threads::Threads)
    
    if(WIN32)
        target_link_libraries(breaklock_server PRIVATE ${SOCKET_LIBS})
    endif()
endif()
