
## Test Case 6
echo -n "Test case 6: Sending 300 packets (with corrupt probability of 30% and drop probability of 30%) and expecting receiver to print them out in order: "
(sleep 0.5; for i in `seq 1 300`; do echo "msg 0 0 Packet: $i"; sleep 0.1; done; sleep 5; echo "exit") | ./tritontalk -d 0.2 -c 0.2 -r 1 -s 1 2> .debug_output.6

(for i in `seq 1 300`; do echo "<RECV_0>:[Packet: $i]"; sleep 0.1; done) > .expected_output.6


