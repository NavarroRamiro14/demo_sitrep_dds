#ifndef SITREP_DATABASE_H
#define SITREP_DATABASE_H

#include <sqlite3.h>
#include <string>
#include <iostream>
#include <cstdio> // Para snprintf

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

    // La magia: INSERT OR REPLACE (Upsert)
    // Si el trackId ya existe, lo actualiza. Si no, lo crea.
    void guardarSitrep(long id, const std::string& source, const std::string& ident, 
                       double lat, double lon, const std::string& info) {
        
        // NOTA: En produccion usar prepared statements (?) para evitar inyeccion SQL.
        // Para esta demo, usamos string formatting simple.
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
};

#endif