/*
 * Operador.cpp - Demo de SITREP para AR-TDC usando OpenDDS
 * Mantiene la lógica de Publicador y Suscriptor en un solo ejecutable.
 */

#include "SitrepTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/SubscriberImpl.h>
#include "SitrepDatabase.h"

#include <iostream>
#include <string>

using namespace ArTdc;

// Variable global para identificar este nodo y filtrar eco
std::string MI_NODO_ID;

// ---------------------------------------------------------------------------
// CLASE LISTENER (Tu código con ligeros ajustes de formato)
// ---------------------------------------------------------------------------
class SitrepListener : public DDS::DataReaderListener {
    SitrepDatabase* db; // Puntero a la base de datos
public:
    // Constructor que recibe la DB
    SitrepListener(SitrepDatabase* _db) : db(_db) {}

    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SitrepMsgDataReader_var reader_i = SitrepMsgDataReader::_narrow(reader);
        if (!reader_i) return;

        SitrepMsg msg;
        DDS::SampleInfo info;

        while (reader_i->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                // FILTRO DE LOOPBACK
                if (std::string(msg.sourceId.in()) == MI_NODO_ID) {
                    continue; 
                }

                db->guardarSitrep(
                    msg.trackId, 
                    msg.sourceId.in(), 
                    msg.identidad.in(), 
                    msg.latitud, 
                    msg.longitud, 
                    msg.infoAmpliatoria.in()
                );

                std::cout << "\n>>> [SITREP RECIBIDO DE " << msg.sourceId << "] <<<" << std::endl;
                std::cout << "    Track ID: " << msg.trackId << " | Identidad: " << msg.identidad << std::endl;
                std::cout << "    Pos: " << msg.latitud << ", " << msg.longitud << std::endl;
                std::cout << "    Info: " << msg.infoAmpliatoria << std::endl;
                std::cout << "-----------------------------------" << std::endl;
                std::cout << "Comando (p: publicar, q: salir): "; 
                std::cout.flush();
            }
        }
    }

    // --- STUBS OBLIGATORIOS (Ahora sí están todos) ---
    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) {} // <--- EL QUE FALTABA
};

class RequestListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
    SnapshotReplyDataWriter_var replyWriter;
    std::string miNodoId;

public:
    RequestListener(SitrepDatabase* _db, SnapshotReplyDataWriter_var _writer, std::string _id) 
        : db(_db), replyWriter(_writer), miNodoId(_id) {}

    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SnapshotRequestDataReader_var req_reader = SnapshotRequestDataReader::_narrow(reader);
        if (!req_reader) return;

        SnapshotRequest msg;
        DDS::SampleInfo info;

        while (req_reader->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                // ¡Alguien pidió datos!
                std::cout << "\n[SOLICITUD] El nodo " << msg.requesterId << " pide Snapshot." << std::endl;

                // 1. Obtener todo de la DB (Necesitas implementar este método en SitrepDatabase.h)
                // std::vector<SitrepMsg> lista = db->obtenerTodos(); 
                // Por ahora simulamos datos para que compile:
                SitrepList listaDDS;
                listaDDS.length(1); 
                listaDDS[0].trackId = 999; 
                listaDDS[0].infoAmpliatoria = CORBA::string_dup("SNAPSHOT_DATA");

                // 2. Preparar respuesta
                SnapshotReply reply;
                reply.targetId = msg.requesterId;
                reply.tracks = listaDDS;

                // 3. ENVIAR (DDS decidirá si este nodo es el Líder basándose en MI_PRIORIDAD)
                replyWriter->write(reply, DDS::HANDLE_NIL);
                std::cout << "[SOLICITUD] Respuesta enviada (sujeta a arbitraje DDS)." << std::endl;
            }
        }
    }
    // --- STUBS OBLIGATORIOS (Ahora sí están todos) ---
    virtual void on_requested_deadline_missed(DDS::DataReader_ptr, const DDS::RequestedDeadlineMissedStatus&) {}
    virtual void on_requested_incompatible_qos(DDS::DataReader_ptr, const DDS::RequestedIncompatibleQosStatus&) {}
    virtual void on_sample_rejected(DDS::DataReader_ptr, const DDS::SampleRejectedStatus&) {}
    virtual void on_liveliness_changed(DDS::DataReader_ptr, const DDS::LivelinessChangedStatus&) {}
    virtual void on_subscription_matched(DDS::DataReader_ptr, const DDS::SubscriptionMatchedStatus&) {}
    virtual void on_sample_lost(DDS::DataReader_ptr, const DDS::SampleLostStatus&) {} // <--- EL QUE FALTABA
};

int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            std::cerr << "USO: ./operador <PRIORIDAD> (Mas alta = mas prioridad)" << std::endl;
            return 1;
        }
        int MI_PRIORIDAD = std::stoi(argv[1]);

        // 1. Inicializar DDS y DB
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        SitrepDatabase miBaseDeDatos("artdc_tactical.db");

        DDS::DomainParticipant_var participant =
            dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        
        if (!participant) return 1;

        // --- CORRECCION: CREAR PUBLISHER Y SUBSCRIBER AQUI ARRIBA ---
        DDS::Publisher_var pub =
            participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        
        DDS::Subscriber_var sub =
            participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        // -----------------------------------------------------------

        // 2. Registrar Tipos Snapshot y Crear Tópicos Snapshot
        SnapshotRequestTypeSupport_var req_ts = new SnapshotRequestTypeSupportImpl;
        req_ts->register_type(participant, "");
        SnapshotReplyTypeSupport_var rep_ts = new SnapshotReplyTypeSupportImpl;
        rep_ts->register_type(participant, "");

        DDS::Topic_var req_topic = participant->create_topic("RequestTopic", req_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);
        DDS::Topic_var rep_topic = participant->create_topic("ReplyTopic", rep_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);

        // 3. Configurar Writer de Respuesta (AQUI YA EXISTE 'pub')
        DDS::DataWriterQos reply_dw_qos;
        pub->get_default_datawriter_qos(reply_dw_qos); // <--- AHORA SI FUNCIONA
        reply_dw_qos.ownership.kind = DDS::EXCLUSIVE_OWNERSHIP_QOS;
        reply_dw_qos.ownership_strength.value = MI_PRIORIDAD;

        DDS::DataWriter_var reply_generic_writer = pub->create_datawriter(rep_topic, reply_dw_qos, 0, 0);
        SnapshotReplyDataWriter_var reply_writer = SnapshotReplyDataWriter::_narrow(reply_generic_writer);

        // 4. Configurar Reader de Solicitudes (AQUI YA EXISTE 'sub')
        DDS::DataReaderListener_var req_listener(new RequestListener(&miBaseDeDatos, reply_writer, MI_NODO_ID));
        DDS::DataReader_var req_reader = sub->create_datareader(req_topic, DATAREADER_QOS_DEFAULT, req_listener, 0);

        // 5. Configurar Sitrep Normal (Sigue igual...)
        SitrepMsgTypeSupport_var ts = new SitrepMsgTypeSupportImpl;
        ts->register_type(participant, "");
        
        CORBA::String_var type_name = ts->get_type_name();
        DDS::Topic_var topic =
            participant->create_topic("SitrepTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataWriter_var writer =
            pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        SitrepMsgDataWriter_var sitrep_writer = SitrepMsgDataWriter::_narrow(writer);

        DDS::DataReaderListener_var listener(new SitrepListener(&miBaseDeDatos));
        DDS::DataReader_var reader =
            sub->create_datareader(topic, DATAREADER_QOS_DEFAULT, listener, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        // -----------------------------------------------------------------------
        // INTERFAZ DE USUARIO (CONSOLA)
        // -----------------------------------------------------------------------
        std::cout << "=== AR-TDC DEMO OPERADOR ===" << std::endl;
        std::cout << "Ingrese el ID de este nodo (ej: NODO_ALFA, PUENTE): ";
        std::cin >> MI_NODO_ID;

        bool corriendo = true;
        while (corriendo) {
            std::cout << "\nComando (p: publicar, q: salir): ";
            char cmd;
            std::cin >> cmd;

            if (cmd == 'q') {
                corriendo = false;
            } else if (cmd == 'p') {
                SitrepMsg msg;
                msg.sourceId = MI_NODO_ID.c_str();

                std::cout << ">> Track ID (numero): ";
                std::cin >> msg.trackId;
                
                std::cout << ">> Identidad (AMIGO/HOSTIL): ";
                std::string s; std::cin >> s;
                msg.identidad = s.c_str();

                std::cout << ">> Latitud: ";
                std::cin >> msg.latitud;

                std::cout << ">> Longitud: ";
                std::cin >> msg.longitud; // Hardcodeado para test rápido

                std::cout << ">> Info Ampliatoria (sin espacios por ahora): ";
                std::string info; std::cin >> info;
                msg.infoAmpliatoria = info.c_str();

                // --- NUEVO: GUARDAR EN MI PROPIA DB ANTES DE ENVIAR ---
                // Usamos la misma instancia 'miBaseDeDatos' que creamos al principio del main
                miBaseDeDatos.guardarSitrep(
                    msg.trackId,
                    MI_NODO_ID,          // Soy yo
                    msg.identidad.in(),
                    msg.latitud,
                    msg.longitud,
                    msg.infoAmpliatoria.in()
                );

                // Publicar el mensaje
                DDS::ReturnCode_t ret = sitrep_writer->write(msg, DDS::HANDLE_NIL);
                if (ret == DDS::RETCODE_OK) {
                    std::cout << " [SITREP ENVIADO EXITOSAMENTE]" << std::endl;
                } else {
                    std::cerr << " [ERROR AL ENVIAR]" << std::endl;
                }
            }
        }

        // Limpieza
        participant->delete_contained_entities();
        dpf->delete_participant(participant);
        TheServiceParticipant->shutdown();

    } catch (const CORBA::Exception& e) {
        e._tao_print_exception("Excepcion CORBA en el main: ");
        return 1;
    }

    return 0;
}