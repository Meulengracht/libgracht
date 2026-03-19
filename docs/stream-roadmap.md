# libgracht Stream Protocol v1 Roadmap

## Motivation
The motivation behind the libgracht Stream Protocol v1 is to provide a solid foundation for handling stream-based data effectively while ensuring reliability and robustness across varying transport mechanisms.

## Goals
- Create first-class streams.
- Ensure reliable streams are reliable, resumable, and checksummed.
- Implement live streams that are resumable but not reliable and signal gaps.
- Provide a receiver API that is callback-based.
- Require negotiated bounds on stream capabilities.
- Ensure that ordinary RPC/control traffic is not starved.
- Maintain transport-agnostic design principles.

## Non-goals
- Establishing a specific underlying transport mechanism.
- Overly complicating the protocol with unnecessary features that do not align with the core goals.

## Stream kinds
### Reliable stream
- Characteristics: reliable, resumable, checksummed.
- Considerations for implementation: error handling and recovery, ensuring stream integrity.

### Live stream
- Characteristics: resumable but not reliable, must signal gaps in the data.
- Design considerations: how to manage data continuity and user expectations for real-time data flow.

## Capability negotiation
- Clearly define the capabilities that streams can negotiate, including bounds and performance metrics.
- Ensure compatibility and flexibility for various implementations.

## Common control frames
- Define control frames that can be used across both reliable and live streams to manage state and operations effectively.

## Reliable stream protocol
- Detailed specification of how reliable streams will function, highlighting checksumming, resumability, and reliability guarantees.

## Live stream protocol
- Definition of how live streams will operate, focusing on the ability to signal gaps and manage non-reliability.

## Callback-based receiver API
- A detailed outline of the callback-based API for receiving streams, specifying event-driven programming considerations.

## Flow control and fairness
- Methods to manage flow control, ensuring fairness in resource allocation among streams and preventing starvation of RPC/control traffic.

## Error model
- Developing a model for expected errors and failures, including resilience strategies for both stream types.

## Security and robustness
- Guidelines for ensuring security practices are embedded within the protocol.

## Milestones
- **Capability negotiation**: Define requirements and implement initial negotiation models.
- **Stream control protocol**: Establish foundational protocols for managing streams.
- **Runtime state machine**: Design a state machine to manage the lifecycle of streams.
- **Reliable streams**: Implementation and testing of reliable stream features.
- **Live streams**: Design and implement features for live streams.
- **Generator/schema support**: Integrate support for schema-driven data generation.
- **Scheduling/fairness**: Implement scheduling strategies for stream fairness.
- **Hardening/testing**: Rigorous testing and security hardening of the protocol.
- **Docs/examples**: Create comprehensive documentation and examples to support developers.

## Agent assignments
- Assign roles and responsibilities for the implementation of various sections and features, ensuring clear ownership and accountability throughout the development process.
