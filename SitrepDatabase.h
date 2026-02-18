#ifndef SITREP_DATABASE_H
#define SITREP_DATABASE_H

#include <sqlite3.h>
#include <string>
#include <iostream>
#include <cstdio>
#include <vector>
#include "SitrepTypeSupportImpl.h"

class SitrepDatabase {
    sqlite3* db;

public:
    SitrepDatabase(const std::string& dbName) {
        int rc = sqlite3_open(dbName.c_str(), &db);
        if (rc) {
            std::cerr << "Error al abrir BD: " << sqlite3_errmsg(db) << std::endl;
        } else {
            std::cout << "Base de datos conectada: " << dbName << std::endl;
            inicializarTabla();
        }
    }

    ~SitrepDatabase() {
        sqlite3_close(db);
    }

    void inicializarTabla() {
        const char* sql = 
            "CREATE TABLE IF NOT EXISTS sitreps ("
            "trackId INTEGER PRIMARY KEY, "
            "sourceId TEXT, "
            "identidad TEXT, "
            "latitud REAL, "
            "longitud REAL, "
            "info TEXT, "
            "timestamp DATETIME DEFAULT CURRENT_TIMESTAMP);";

        char* errMsg = 0;
        if (sqlite3_exec(db, sql, 0, 0, &errMsg) != SQLITE_OK) {
            std::cerr << "SQL Error: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        }
    }

    void guardarSitrep(long id, const std::string& source, const std::string& ident, 
                       double lat, double lon, const std::string& info) {
        char sql[512];
        snprintf(sql, sizeof(sql),
            "INSERT OR REPLACE INTO sitreps (trackId, sourceId, identidad, latitud, longitud, info) "
            "VALUES (%ld, '%s', '%s', %f, %f, '%s');",
            id, source.c_str(), ident.c_str(), lat, lon, info.c_str());

        char* errMsg = 0;
        if (sqlite3_exec(db, sql, 0, 0, &errMsg) != SQLITE_OK) {
            std::cerr << "Error al guardar SITREP: " << errMsg << std::endl;
            sqlite3_free(errMsg);
        } else {
            std::cout << "   [DB] Track " << id << " persistido correctamente." << std::endl;
        }
    }

    // Retorna todos los registros actuales para enviarlos a un nodo nuevo
    std::vector<ArTdc::SitrepMsg> obtenerSitrepsParaSnapshot() {
        std::vector<ArTdc::SitrepMsg> lista;
        const char* sql = "SELECT trackId, sourceId, identidad, latitud, longitud, info FROM sitreps;";
        sqlite3_stmt* stmt;

        if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK) {
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                ArTdc::SitrepMsg msg;
                msg.trackId = sqlite3_column_int(stmt, 0);
                msg.sourceId = (const char*)sqlite3_column_text(stmt, 1);
                msg.identidad = (const char*)sqlite3_column_text(stmt, 2);
                msg.latitud = sqlite3_column_double(stmt, 3);
                msg.longitud = sqlite3_column_double(stmt, 4);
                msg.infoAmpliatoria = (const char*)sqlite3_column_text(stmt, 5);
                lista.push_back(msg);
            }
        }
        sqlite3_finalize(stmt);
        return lista;
    }
};

#endif