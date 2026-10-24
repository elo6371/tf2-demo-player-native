use bitbuffer::BitRead;
use main_error::MainError;
use std::env;
use std::fs;
use tf_demo_parser::demo::gameevent_gen::{GameEvent, GameEventType};
use tf_demo_parser::demo::gamevent::{GameEventDefinition, GameEventValue};
use tf_demo_parser::demo::header::Header;
use tf_demo_parser::demo::message::Message;
use tf_demo_parser::demo::packet::Packet;
use tf_demo_parser::demo::parser::{ParserState, RawPacketStream};
use tf_demo_parser::{Demo, MessageType};

fn no_event_messages(_message_type: MessageType) -> bool {
    false
}

fn game_event_messages(message_type: MessageType) -> bool {
    matches!(message_type, MessageType::GameEvent)
}

fn sanitize(value: &str) -> String {
    value
        .chars()
        .map(|c| match c {
            '\t' | '\n' | '\r' => ' ',
            ';' => ',',
            _ => c,
        })
        .collect()
}

fn format_value(value: &GameEventValue) -> String {
    match value {
        GameEventValue::String(value) => sanitize(&value.to_string()),
        GameEventValue::Float(value) => format!("{}", value),
        GameEventValue::Long(value) => value.to_string(),
        GameEventValue::Short(value) => value.to_string(),
        GameEventValue::Byte(value) => value.to_string(),
        GameEventValue::Boolean(value) => value.to_string(),
        GameEventValue::Local => String::new(),
    }
}

// Keep the string table metadata in the parser state: svc_UpdateStringTable
// decoding looks the table up by index.
fn update_string_tables(state: &mut ParserState, packet: &Packet) {
    match packet {
        Packet::Message(packet) | Packet::Signon(packet) => {
            for message in &packet.messages {
                if let Message::CreateStringTable(create) = message {
                    state.handle_string_table_meta(create.table.get_table_meta());
                }
            }
        }
        Packet::StringTables(packet) => {
            for table in &packet.tables {
                state.handle_string_table_meta(table.get_table_meta());
            }
        }
        _ => {}
    }
}

fn main() -> Result<(), MainError> {
    let args: Vec<String> = env::args().collect();
    if args.len() < 2 {
        println!("usage: dump_events <demo> [sha256]");
        return Ok(());
    }
    let path = args[1].clone();
    let sha256 = args.get(2).cloned().unwrap_or_default();
    let file = fs::read(&path)?;
    let demo = Demo::new(&file);
    let header_info = {
        let mut stream = demo.get_stream();
        let header = Header::read(&mut stream)?;
        (header.map, header.ticks, header.protocol, header.demo_type)
    };

    // Pass 1: read only the event definitions (GameEventList) sent during signon.
    let mut definitions: Vec<GameEventDefinition> = Vec::new();
    {
        let mut stream = demo.get_stream();
        let header = Header::read(&mut stream)?;
        let mut state = ParserState::new(header.protocol, no_event_messages, false);
        let mut packets = RawPacketStream::new(stream);
        while let Some(packet) = packets.next(&state)? {
            update_string_tables(&mut state, &packet);
            if let Packet::Message(packet) | Packet::Signon(packet) = &packet {
                for message in &packet.messages {
                    if let Message::GameEventList(list) = message {
                        definitions = list.event_list.clone();
                    }
                }
            }
            if !definitions.is_empty() {
                break;
            }
        }
    }

    // Pass 2: decode every game event as a raw positional event so the values keep
    // their wire order and can be paired with the definitions from pass 1.
    let mut stream = demo.get_stream();
    let header = Header::read(&mut stream)?;
    let mut state = ParserState::new(header.protocol, game_event_messages, false);
    state.event_definitions = definitions
        .iter()
        .map(|definition| GameEventDefinition {
            id: definition.id,
            event_type: GameEventType::Unknown(definition.event_type.as_str().to_string()),
            entries: definition.entries.clone(),
        })
        .collect();

    let mut events: Vec<(u32, u16, String, String)> = Vec::new();
    let mut unparsed = 0u64;
    let mut packets = RawPacketStream::new(stream);
    while let Some(packet) = packets.next(&state)? {
        update_string_tables(&mut state, &packet);
        if let Packet::Message(packet) | Packet::Signon(packet) = &packet {
            let tick = u32::from(packet.tick);
            for message in &packet.messages {
                if let Message::GameEvent(event) = message {
                    let event_id = u16::from(event.event_type_id);
                    match (definitions.get(event_id as usize), &event.event) {
                        (Some(definition), GameEvent::Unknown(raw)) => {
                            let mut fields = String::new();
                            for (entry, value) in definition.entries.iter().zip(raw.values.iter()) {
                                if !fields.is_empty() {
                                    fields.push(';');
                                }
                                fields.push_str(&entry.name);
                                fields.push('=');
                                fields.push_str(&format_value(value));
                            }
                            events.push((
                                tick,
                                event_id,
                                definition.event_type.as_str().to_string(),
                                fields,
                            ));
                        }
                        _ => {
                            unparsed += 1;
                        }
                    }
                }
            }
        }
    }

    println!(
        "# demo={} sha256={} bytes={} events={}",
        path,
        sha256,
        file.len(),
        events.len()
    );
    println!(
        "# header map={} ticks={} protocol={} demo_type={}",
        header_info.0, header_info.1, header_info.2, header_info.3
    );
    for definition in &definitions {
        let fields: Vec<String> = definition
            .entries
            .iter()
            .map(|entry| format!("{}:{}", entry.name, entry.kind))
            .collect();
        println!(
            "# def id={} name={} fields={}",
            u16::from(definition.id),
            definition.event_type.as_str(),
            fields.join(",")
        );
    }
    for (tick, event_id, name, fields) in &events {
        println!("{}\t{}\t{}\t{}", tick, event_id, name, fields);
    }
    if unparsed > 0 {
        eprintln!("unparsed game events: {}", unparsed);
    }
    Ok(())
}
