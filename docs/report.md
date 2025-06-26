反序列化消息：
1. RaftNode <-> Transport
2. Pipeline -> Transport

序列化消息：
1. Transport -> Pipeline

- RaftNode生产消息到Transport中，且从Transport取走由Pipeline接收的
- Pipeline接收消息并反序列化并放入Transport的队列中，这些消息被Raftnode取走
- Transport只负责处理RaftNode生产的消息，并将其序列化之后放入Pipeline中
- Pipeline只负责发送Transport放入的序列化消息和接收网络的消息并反序列化放入Transport中送往RaftNode的队列中
- RaftNode只负责处理Transport中送往自己的队列中的消息和生产消息到Transport中