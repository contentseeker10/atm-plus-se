/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: http_server.cpp
 * Implementation of HTTP server adapter.
 *
 */

#include "http_server.hpp"

#include <print>

void HttpServerAdapter::_mount_frontend() {
    const std::filesystem::path web_root = VIEW_PATH;
    if (!std::filesystem::exists(web_root)) {
        std::println("frontend directory {} not found", VIEW_PATH);
    }
    if (!_server.set_mount_point("/", VIEW_PATH)) {
        std::println("could not mount frontend");
    }
}

void HttpServerAdapter::_register_routes() {

}