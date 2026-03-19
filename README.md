# Smart TV Telemetry Platform

This project demonstrates a real-time telemetry system for Smart TV devices.

It captures Bluetooth and casting events, streams them using Kafka, and processes them using Apache Flink.

## Tech Stack
- C++ (ingestion)
- Kafka (streaming)
- Apache Flink (processing)

## Status
Initial project setup

📡 Smart TV Telemetry Platform
Real-Time Event Processing for Bluetooth & Casting Systems


🔍 Overview
This project demonstrates the design of a real-time telemetry processing platform for Smart TV devices, focusing on Bluetooth connectivity and casting (screen streaming) events.
It simulates how device-level events are ingested, streamed, and processed using a scalable, event-driven architecture.


🎯 Problem Context
Modern Smart TVs generate continuous telemetry such as:
- Bluetooth device connections (connect/disconnect)
- Casting session events (start/stop streaming)
- Device state and performance signals
- The goal is to design a system that:
- Handles continuous event streams
- Supports real-time processing
- Enables scalable and decoupled architecture
- Reflects production-like system design principles


🏗️ System Architecture
👉 Architecture diagram available in /architecture folder
TV / Device Simulator (C++)
        |
        v
Telemetry Ingestion Service (C++)
        |
        v
Kafka (Event Streaming Layer)
        |
        v
Apache Flink (Stream Processing)
        |
        v
Storage / Output Layer (Planned)


⚙️ Tech Stack
C++ – Telemetry ingestion (device-side simulation)
Kafka – Event streaming platform
Apache Flink – Real-time stream processing
Python (PyFlink) – Processing logic
Docker (Planned) – Containerization


Key Design Principles
Event-Driven Architecture - Decouples ingestion and processing for scalability
Asynchronous Processing - Enables high-throughput handling of device events
Scalability - Kafka-based streaming allows horizontal scaling
Separation of Concerns - Clear division between ingestion, streaming, and processing


🚧 Current Status
✔ Project structure initialized
✔ C++ ingestion service (event simulation)
✔ Flink processing skeleton
🔄 Kafka integration (in progress)
🔄 Stream processing logic (in progress)
🔄 Storage layer (planned)


🧠 Why This Project
This project is an extension of real-world experience in:
Embedded systems
Bluetooth and media streaming
Platform-level development
It focuses on understanding how device-level systems integrate with distributed backend architectures.


🚀 Learning Focus
Distributed systems fundamentals
Kafka-based streaming pipelines
Stream processing with Apache Flink
System design for real-time data


📌 Future Enhancements
Kafka producer integration in C++
Stateful stream processing (Flink windowing)
PostgreSQL / Redis integration
Kubernetes deployment
Monitoring and observability


👨‍💻 Author
Amol Kumar Singh
Senior Technical Lead | System Design
📧 amol0405@gmail.com
🔗 https://linkedin.com/in/amol-singh-906763b
