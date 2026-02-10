/*
 * Operador.cpp - Demo de SITREP para AR-TDC usando OpenDDS
 * Mantiene la lógica de Publicador y Suscriptor en un solo ejecutable.
 */

#include "SitrepTypeSupportImpl.h"
#include <dds/DCPS/Service_Participant.h>
#include <dds/DCPS/Marked_Default_Qos.h>
#include <dds/DCPS/PublisherImpl.h>
#include <dds/DCPS/SubscriberImpl.h>

#include <iostream>
#include <string>

using namespace ArTdc;

// Variable global para identificar este nodo y filtrar eco
std::string MI_NODO_ID;

// ---------------------------------------------------------------------------
// CLASE LISTENER (Tu código con ligeros ajustes de formato)
// ---------------------------------------------------------------------------
class SitrepListener : public DDS::DataReaderListener {
public:
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

// ---------------------------------------------------------------------------
// MAIN
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    try {
        // 1. Inicializar el framework DDS
        DDS::DomainParticipantFactory_var dpf = TheParticipantFactoryWithArgs(argc, argv);

        // 2. Crear el Participante (Dominio 42, arbitrario para AR-TDC)
        DDS::DomainParticipant_var participant =
            dpf->create_participant(42, PARTICIPANT_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        if (!participant) {
            std::cerr << "Error al crear el participante." << std::endl;
            return 1;
        }

        // 3. Registrar el tipo de dato (TypeSupport) definido en el IDL
        SitrepMsgTypeSupport_var ts = new SitrepMsgTypeSupportImpl;
        if (ts->register_type(participant, "") != DDS::RETCODE_OK) {
            std::cerr << "Error al registrar TypeSupport." << std::endl;
            return 1;
        }

        // 4. Crear el Tópico "SitrepTopic"
        CORBA::String_var type_name = ts->get_type_name();
        DDS::Topic_var topic =
            participant->create_topic("SitrepTopic", type_name, TOPIC_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        // 5. Configurar PUBLICADOR
        DDS::Publisher_var pub =
            participant->create_publisher(PUBLISHER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        
        DDS::DataWriter_var writer =
            pub->create_datawriter(topic, DATAWRITER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);
        
        // "Narrow" convierte el writer genérico al writer específico de Sitrep
        SitrepMsgDataWriter_var sitrep_writer = SitrepMsgDataWriter::_narrow(writer);

        // 6. Configurar SUSCRIPTOR
        DDS::Subscriber_var sub =
            participant->create_subscriber(SUBSCRIBER_QOS_DEFAULT, 0, OpenDDS::DCPS::DEFAULT_STATUS_MASK);

        DDS::DataReaderListener_var listener(new SitrepListener);
        
        // Creamos el lector y le asignamos el Listener
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