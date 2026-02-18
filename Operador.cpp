#include "SitrepTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/SubscriberImpl.h>
#include "SitrepDatabase.h"
#include <iostream>
#include <string>

using namespace ArTdc;
std::string MI_NODO_ID;

// 1. Recibe actualizaciones de SITREPS normales
class SitrepListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
public:
    SitrepListener(SitrepDatabase* _db) : db(_db) {}
    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SitrepMsgDataReader_var reader_i = SitrepMsgDataReader::_narrow(reader);
        SitrepMsg msg;
        DDS::SampleInfo info;
        while (reader_i->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data && std::string(msg.sourceId.in()) != MI_NODO_ID) {
                db->guardarSitrep(msg.trackId, msg.sourceId.in(), msg.identidad.in(), msg.latitud, msg.longitud, msg.infoAmpliatoria.in());
                std::cout << "\n>>> [SITREP RECIBIDO DE " << msg.sourceId << "] <<<" << std::endl;
                std::cout << "Comando (p: publicar, q: salir): "; std::cout.flush();
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

// 2. Responde cuando un nodo nuevo pide la base de datos (Lógica de Líder)
class RequestListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
    SnapshotReplyDataWriter_var replyWriter;
public:
    RequestListener(SitrepDatabase* _db, SnapshotReplyDataWriter_var _writer) : db(_db), replyWriter(_writer) {}
    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SnapshotRequestDataReader_var req_reader = SnapshotRequestDataReader::_narrow(reader);
        SnapshotRequest req;
        DDS::SampleInfo info;
        while (req_reader->take_next_sample(req, info) == DDS::RETCODE_OK) {
            if (info.valid_data) {
                std::cout << "\n[SINCRO] El nodo " << req.requesterId << " pide Snapshot." << std::endl;
                std::vector<SitrepMsg> datos = db->obtenerSitrepsParaSnapshot();
                
                SnapshotReply reply;
                reply.targetId = req.requesterId;
                reply.tracks.length(static_cast<CORBA::ULong>(datos.size()));
                for(size_t i=0; i<datos.size(); ++i) reply.tracks[static_cast<CORBA::ULong>(i)] = datos[i];

                replyWriter->write(reply, DDS::HANDLE_NIL);
                std::cout << "[SINCRO] Snapshot enviado (bajo arbitraje de Ownership)." << std::endl;
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

// 3. Recibe la base de datos del líder al iniciar
class ReplyListener : public DDS::DataReaderListener {
    SitrepDatabase* db;
public:
    ReplyListener(SitrepDatabase* _db) : db(_db) {}
    virtual void on_data_available(DDS::DataReader_ptr reader) {
        SnapshotReplyDataReader_var rep_reader = SnapshotReplyDataReader::_narrow(reader);
        SnapshotReply msg;
        DDS::SampleInfo info;
        while (rep_reader->take_next_sample(msg, info) == DDS::RETCODE_OK) {
            if (info.valid_data && std::string(msg.targetId.in()) == MI_NODO_ID) {
                std::cout << "\n[SINCRO] Recibido snapshot con " << msg.tracks.length() << " registros." << std::endl;
                for (unsigned int i = 0; i < msg.tracks.length(); ++i) {
                    db->guardarSitrep(msg.tracks[i].trackId, msg.tracks[i].sourceId.in(), 
                                      msg.tracks[i].identidad.in(), msg.tracks[i].latitud, 
                                      msg.tracks[i].longitud, msg.tracks[i].infoAmpliatoria.in());
                }
                std::cout << "[SINCRO] Base de datos sincronizada con éxito." << std::endl;
                std::cout << "\nComando (p: publicar, q: salir): "; std::cout.flush();
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
        if (argc < 2) { std::cerr << "USO: ./operador <PRIORIDAD>" << std::endl; return 1; }
        int MI_PRIORIDAD = std::stoi(argv[1]);

        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);
        SitrepDatabase miBaseDeDatos("artdc_tactical.db");
        DDS::DomainParticipant_var participant = dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        
        DDS::Publisher_var pub = participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        DDS::Subscriber_var sub = participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // Registro de Tipos y Tópicos
        SitrepMsgTypeSupport_var ts = new SitrepMsgTypeSupportImpl; ts->register_type(participant, "");
        SnapshotRequestTypeSupport_var req_ts = new SnapshotRequestTypeSupportImpl; req_ts->register_type(participant, "");
        SnapshotReplyTypeSupport_var rep_ts = new SnapshotReplyTypeSupportImpl; rep_ts->register_type(participant, "");

        DDS::Topic_var topic = participant->create_topic("SitrepTopic", ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);
        DDS::Topic_var req_topic = participant->create_topic("RequestTopic", req_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);
        DDS::Topic_var rep_topic = participant->create_topic("ReplyTopic", rep_ts->get_type_name(), TOPIC_QOS_DEFAULT, 0, 0);

        // QoS con EXCLUSIVE OWNERSHIP para la respuesta del Líder
        DDS::DataWriterQos reply_dw_qos;
        pub->get_default_datawriter_qos(reply_dw_qos);
        reply_dw_qos.ownership.kind = DDS::EXCLUSIVE_OWNERSHIP_QOS;
        reply_dw_qos.ownership_strength.value = MI_PRIORIDAD;

        // Writers
        SnapshotReplyDataWriter_var reply_writer = SnapshotReplyDataWriter::_narrow(pub->create_datawriter(rep_topic, reply_dw_qos, 0, 0));
        SnapshotRequestDataWriter_var request_writer = SnapshotRequestDataWriter::_narrow(pub->create_datawriter(req_topic, DATAWRITER_QOS_DEFAULT, 0, 0));
        SitrepMsgDataWriter_var sitrep_writer = SitrepMsgDataWriter::_narrow(pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, 0));

        // Readers y Listeners
        DDS::DataReaderListener_var sitrep_l(new SitrepListener(&miBaseDeDatos));
        sub->create_datareader(topic, DATAREADER_QOS_DEFAULT, sitrep_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataReaderListener_var req_l(new RequestListener(&miBaseDeDatos, reply_writer));
        sub->create_datareader(req_topic, DATAREADER_QOS_DEFAULT, req_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataReaderListener_var rep_l(new ReplyListener(&miBaseDeDatos));
        sub->create_datareader(rep_topic, DATAREADER_QOS_DEFAULT, rep_l, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        std::cout << "=== AR-TDC DEMO OPERADOR (Prioridad: " << MI_PRIORIDAD << ") ===" << std::endl;
        std::cout << "Ingrese el ID de este nodo: "; std::cin >> MI_NODO_ID;

        // --- SOLICITUD DE SNAPSHOT AL INICIAR ---
        SnapshotRequest req_msg; req_msg.requesterId = MI_NODO_ID.c_str();
        request_writer->write(req_msg, DDS::HANDLE_NIL);
        std::cout << "[SINCRO] Solicitando snapshot de red..." << std::endl;

        while (true) {
            std::cout << "\nComando (p: publicar, q: salir): ";
            char cmd; std::cin >> cmd;
            if (cmd == 'q') break;
            if (cmd == 'p') {
                SitrepMsg msg; msg.sourceId = MI_NODO_ID.c_str();
                std::cout << ">> Track ID (numero): "; std::cin >> msg.trackId;
                std::cout << ">> Identidad (AMIGO/HOSTIL): "; { std::string s; std::cin >> s; msg.identidad = s.c_str(); }
                std::cout << ">> Latitud: "; std::cin >> msg.latitud;
                std::cout << ">> Longitud: "; std::cin >> msg.longitud;
                std::cout << ">> Info Ampliatoria: "; { std::string s; std::cin >> s; msg.infoAmpliatoria = s.c_str(); }

                miBaseDeDatos.guardarSitrep(msg.trackId, MI_NODO_ID, msg.identidad.in(), msg.latitud, msg.longitud, msg.infoAmpliatoria.in());
                sitrep_writer->write(msg, DDS::HANDLE_NIL);
            }
        }

        participant->delete_contained_entities();
        dpf->delete_participant(participant);
        TheServiceParticipant->shutdown();
    } catch (const CORBA::Exception& e) { e._tao_print_exception("Error en main:"); return 1; }
    return 0;
}