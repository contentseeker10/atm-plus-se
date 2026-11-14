/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: http_server.hpp
 * Interface for HTTP server adapter.
 *
 */

#pragma once

#include <httplib.h>

#include <filesystem>

inline constexpr auto HOST      = "0.0.0.0";
inline constexpr auto VIEW_PATH = "../../../view";

class AtmController;

class HttpServerAdapter
{
public:
    HttpServerAdapter(std::shared_ptr<AtmController> controller, int port = 8080)
        : _atm_controller{controller}, _port{port}
    { }

    void start() { 
        _mount_frontend();
        _register_routes();
        _server.listen(HOST, _port); 
    }
    void stop() noexcept { _server.stop(); }

    
    private:
    httplib::Server _server;
    int _port{8080};
    
    std::shared_ptr<AtmController> _atm_controller;
    
    void _mount_frontend();

    void _register_routes();
};