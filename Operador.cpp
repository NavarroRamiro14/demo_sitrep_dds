#include "SitrepTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/SubscriberImpl.h>
#include "SitrepDatabase.h"
#include <iostream>
#include <string>
#include <vector>
#include <thread> // Necesario para sleep
#include <chrono> // Necesario para tiempos

using namespace ArTdc;

// Variable global para identificar este proceso
std::string MI_NODO_ID;

/**
 * LISTENER 1: SITREPS (Actualizaciones en tiempo real)
 * - Escucha actualizaciones de posición de otros nodos.
 * - Ignora las propias.
 */
class SitrepListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
public:
    SitrepListener(SitrepDatabase* _db) : db(_db) {}

    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SitrepMsgDataReader_var reader_i = SitrepMsgDataReader::_narrow(reader);
        SitrepMsg msg;
        DDS::SampleInfo info;

        while (reader_i->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                std::string origen = msg.sourceId.in();

                // 1. Supresión de Eco: Si soy yo mismo, ignoro
                if (origen == MI_NODO_ID) {
                    return; 
                }

                // 2. Log Verbal
                std::cout << "\n>>> [INFO] Recibido SITREP de nodo: " << origen << std::endl;
                std::cout << "    | TrackID: " << msg.trackId << " (" << msg.identidad << ")" << std::endl;
                std::cout << "    | Pos: " << msg.latitud << ", " << msg.longitud << std::endl;

                // 3. Guardar en Base de Datos
                db->guardarSitrep(msg.trackId, origen, msg.identidad.in(), msg.latitud, msg.longitud, msg.infoAmpliatoria.in());
                
                // Restaurar el prompt visual
                std::cout << "Comando (p: publicar, l: listar, q: salir): "; std::cout.flush();
            }
        }
    }

    // Métodos vacíos obligatorios
    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) {}
};

/**
 * LISTENER 2: SNAPSHOT REQUEST (Lógica del Líder/Veterano)
 * - Escucha si alguien nuevo pide la base de datos completa.
 * - Lee la DB local y manda un REPLY.
 */
class RequestListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
    SnapshotReplyDataWriter_var replyWriter;
public:
    RequestListener(SitrepDatabase* _db, SnapshotReplyDataWriter_var _writer) 
        : db(_db), replyWriter(_writer) {}

    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SnapshotRequestDataReader_var req_reader = SnapshotRequestDataReader::_narrow(reader);
        SnapshotRequest req;
        DDS::SampleInfo info;

        while (req_reader->take_next_sample(req, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                std::string solicitante = req.requesterId.in();

                // 1. Supresión de Eco: No me respondo a mí mismo
                if (solicitante == MI_NODO_ID) {
                    return;
                }

                std::cout << "\n[SINCRO] Solicitud de Snapshot recibida de: " << solicitante << std::endl;

                // 2. Lógica de Negocio: Leer DB local
                std::vector<SitrepMsg> datosLocales = db->obtenerSitrepsParaSnapshot();
                
                if (datosLocales.empty()) {
                     std::cout << "[SINCRO] Mi base de datos está vacía. No envío nada." << std::endl;
                     std::cout << "Comando (p: publicar, l: listar, q: salir): "; std::cout.flush();
                     return;
                }

                std::cout << "[SINCRO] Preparando envío de " << datosLocales.size() << " tracks..." << std::endl;

                // 3. Preparar Respuesta (Convertir Vector C++ -> Secuencia CORBA)
                SnapshotReply reply;
                reply.targetId = req.requesterId; // Dirigido solo al que preguntó
                reply.tracks.length(static_cast<CORBA::ULong>(datosLocales.size()));

                for(size_t i=0; i<datosLocales.size(); ++i) {
                    reply.tracks[static_cast<CORBA::ULong>(i)] = datosLocales[i];
                }

                // 4. Enviar
                replyWriter->write(reply, DDS::HANDLE_NIL);
                std::cout << "[SINCRO] Snapshot enviado a " << solicitante << "." << std::endl;
                std::cout << "Comando (p: publicar, l: listar, q: salir): "; std::cout.flush();
            }
        }
    }

    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) {}
};

/**
 * LISTENER 3: SNAPSHOT REPLY (Lógica del Novato)
 * - Recibe la base de datos masiva.
 * - Filtra si el mensaje no era para mí.
 */
class ReplyListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
public:
    ReplyListener(SitrepDatabase* _db) : db(_db) {}

    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SnapshotReplyDataReader_var rep_reader = SnapshotReplyDataReader::_narrow(reader);
        SnapshotReply msg;
        DDS::SampleInfo info;

        while (rep_reader->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                std::string destinatario = msg.targetId.in();

                // 1. Filtro: ¿Es para mí?
                if (destinatario != MI_NODO_ID) {
                    // Veo pasar el paquete, pero lo ignoro silenciosamente (o logueo debug)
                    return;
                }

                std::cout << "\n[SINCRO] <<< Recibido PAQUETE SNAPSHOT >>>" << std::endl;
                std::cout << "[SINCRO] Procesando " << msg.tracks.length() << " tracks..." << std::endl;

                // 2. Insertar todo en la DB
                for (unsigned int i = 0; i < msg.tracks.length(); ++i) {
                    db->guardarSitrep(
                        msg.tracks[i].trackId, 
                        msg.tracks[i].sourceId.in(), 
                        msg.tracks[i].identidad.in(), 
                        msg.tracks[i].latitud, 
                        msg.tracks[i].longitud, 
                        msg.tracks[i].infoAmpliatoria.in()
                    );
                }
                std::cout << "[SINCRO] Sincronización completada exitosamente." << std::endl;
                std::cout << "Comando (p: publicar, l: listar, q: salir): "; std::cout.flush();
            }
        }
    }

    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) {}
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) { 
            std::cerr << "USO: ./operador <PRIORIDAD>" << std::endl; 
            std::cerr << "Ej: ./operador 10 (Prioridad baja) o ./operador 50 (Prioridad alta para responder snapshots)" << std::endl;
            return 1; 
        }
        int MI_PRIORIDAD = std::stoi(argv[1]);

        // Inicializar DDS
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        
        // Conexión a DB SQLite
        SitrepDatabase miBaseDeDatos("artdc_tactical.db");
        
        // Crear Participante
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        if (!participant) {
            std::cerr << "Error al crear participante" << std::endl;
            return 1;
        }

        // Registrar Tipos
        SitrepMsgTypeSupport_var ts = new SitrepMsgTypeSupportImpl;
        if (ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        SnapshotRequestTypeSupport_var req_ts = new SnapshotRequestTypeSupportImpl;
        if (req_ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        SnapshotReplyTypeSupport_var rep_ts = new SnapshotReplyTypeSupportImpl;
        if (rep_ts->register_type(participant, "") != DDS::RETCODE_OK) return 1;

        // Crear Tópicos
        DDS::Topic_var topic = participant->create_topic("SitrepTopic", ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);
        DDS::Topic_var req_topic = participant->create_topic("RequestTopic", req_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);
        DDS::Topic_var rep_topic = participant->create_topic("ReplyTopic", rep_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);

        // Crear Pub/Sub
        DDS::Publisher_var pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Subscriber_var sub = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // --- CONFIGURACIÓN DE ESCRITURA ---
        
        // 1. Writer para SITREPS (Normal)
        SitrepMsgDataWriter_var sitrep_writer = SitrepMsgDataWriter::_narrow(pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, 0));

        // 2. Writer para Solicitudes (Request)
        SnapshotRequestDataWriter_var request_writer = SnapshotRequestDataWriter::_narrow(pub->create_datawriter(req_topic, DATAWRITER_QOS_DEFAULT, 0, 0));

        // 3. Writer para Respuestas (Reply) - USA OWNERSHIP
        // Solo el nodo con mayor prioridad (Strength) podrá escribir validamente en este tópico si hay conflicto.
        DDS::DataWriterQos reply_dw_qos;
        pub->get_default_datawriter_qos(reply_dw_qos);
        reply_dw_qos.ownership.kind = DDS::EXCLUSIVE_OWNERSHIP_QOS;
        reply_dw_qos.ownership_strength.value = MI_PRIORIDAD;
        
        SnapshotReplyDataWriter_var reply_writer = SnapshotReplyDataWriter::_narrow(pub->create_datawriter(rep_topic, reply_dw_qos, 0, 0));


        // --- CONFIGURACIÓN DE LECTURA (LISTENERS) ---

        // 1. Listener SITREPS
        DDS::DataReaderListener_var sitrep_l(new SitrepListener(&miBaseDeDatos));
        sub->create_datareader(topic, DATAREADER_QOS_DEFAULT, sitrep_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // 2. Listener REQUESTS (Necesita el writer para responder)
        DDS::DataReaderListener_var req_l(new RequestListener(&miBaseDeDatos, reply_writer));
        sub->create_datareader(req_topic, DATAREADER_QOS_DEFAULT, req_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // 3. Listener REPLIES (Configurado con OWNERSHIP también para filtrar)
        DDS::DataReaderQos reply_dr_qos;
        sub->get_default_datareader_qos(reply_dr_qos);
        reply_dr_qos.ownership.kind = DDS::EXCLUSIVE_OWNERSHIP_QOS; // Solo acepta del dueño actual

        DDS::DataReaderListener_var rep_l(new ReplyListener(&miBaseDeDatos));
        sub->create_datareader(rep_topic, reply_dr_qos, rep_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);


        // --- UI E INTERACCIÓN ---
        std::cout << "===========================================" << std::endl;
        std::cout << "   AR-TDC DEMO OPERADOR (Prioridad: " << MI_PRIORIDAD << ")   " << std::endl;
        std::cout << "===========================================" << std::endl;
        std::cout << "Ingrese el ID de este nodo (ej: NODO_ALFA): "; 
        std::cin >> MI_NODO_ID;

        // --- CORRECCIÓN: ESPERA DE DESCUBRIMIENTO ---
        // Le damos un respiro al DDS para que encuentre a los otros peers antes de hablar
        std::cout << "[INICIO] Esperando descubrimiento de red (2s)..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));

        // Auto-solicitud de snapshot al iniciar
        SnapshotRequest req_msg; 
        req_msg.requesterId = MI_NODO_ID.c_str();
        request_writer->write(req_msg, DDS::HANDLE_NIL);
        std::cout << "[INICIO] Solicitando snapshot de red..." << std::endl;

        while (true) {
            std::cout << "\nComando (p: publicar, l: listar, q: salir): ";
            char cmd; std::cin >> cmd;
            if (cmd == 'q') break;
            
            // NUEVO COMANDO: LISTAR
            if (cmd == 'l') {
                std::cout << "\n--- BASE DE DATOS LOCAL ---" << std::endl;
                std::vector<SitrepMsg> logs = miBaseDeDatos.obtenerSitrepsParaSnapshot();
                if(logs.empty()) {
                    std::cout << "(Base de datos vacía)" << std::endl;
                } else {
                    for(const auto& s : logs) {
                        std::cout << "Track " << s.trackId 
                                  << " [" << s.identidad << "] de " << s.sourceId 
                                  << " -> (" << s.latitud << ", " << s.longitud << ") " 
                                  << s.infoAmpliatoria << std::endl;
                    }
                }
                std::cout << "---------------------------" << std::endl;
            }

            if (cmd == 'p') {
                SitrepMsg msg; 
                msg.sourceId = MI_NODO_ID.c_str();
                
                std::cout << ">> Track ID (entero): "; std::cin >> msg.trackId;
                std::cout << ">> Identidad (AMIGO/HOSTIL): "; { std::string s; std::cin >> s; msg.identidad = s.c_str(); }
                std::cout << ">> Latitud: "; std::cin >> msg.latitud;
                std::cout << ">> Longitud: "; std::cin >> msg.longitud;
                std::cout << ">> Info Ampliatoria: "; { std::string s; std::cin >> s; msg.infoAmpliatoria = s.c_str(); }

                // Guardar localmente antes de enviar
                miBaseDeDatos.guardarSitrep(msg.trackId, MI_NODO_ID, msg.identidad.in(), msg.latitud, msg.longitud, msg.infoAmpliatoria.in());
                
                // Enviar a la red
                sitrep_writer->write(msg, DDS::HANDLE_NIL);
                std::cout << "[INFO] Sitrep publicado." << std::endl;
            }
        }

        // Limpieza
        participant->delete_contained_entities();
        dpf->delete_participant(participant);
        TheServiceParticipant->shutdown();

    } catch (const CORBA::Exception& e) { 
        e._tao_print_exception("Excepción CORBA en main:"); 
        return 1; 
    }
    return 0;
}