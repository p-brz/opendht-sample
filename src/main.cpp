#include <opendht.h>
#include <opendht/node.h>
#include <vector>
#include <iostream>

#include <unistd.h>

using namespace std;

ostream & operator<<(ostream & out, dht::NodeStatus stat){
    switch (stat) {
    case dht::NodeStatus::Connected:
        out << "Connected";
        break;
    case dht::NodeStatus::Connecting:
        out << "Connecting";
        break;
    case dht::NodeStatus::Disconnected:
        out << "Disconnected";
        break;
    }

    return out;
}

struct snode_compare {
    bool operator() (const std::shared_ptr<dht::Node>& lhs, const std::shared_ptr<dht::Node>& rhs) const{
        return (lhs->id < rhs->id) ||
            (lhs->id == rhs->id && lhs->getFamily() == AF_INET && rhs->getFamily() == AF_INET6);
    }
};

using NodeSet = std::set<std::shared_ptr<dht::Node>, snode_compare>;
std::condition_variable cv;

void
step(dht::DhtRunner& dht, std::atomic_uint& done, std::shared_ptr<NodeSet> all_nodes, dht::InfoHash cur_h, unsigned cur_depth);
void printAddresses(dht::DhtRunner & node);
bool addrToString(const sockaddr_storage * addr, std::string & outHost, std::string & outPort);

void scanNetwork();

void processInput();

dht::DhtRunner node;
    
volatile dht::NodeStatus previousV4Status = dht::NodeStatus::Disconnected;    
    
int main(){
    // Launch a dht node on a new thread, using a
    // generated RSA key pair, and listen on port 4222.
    node.run(4222, dht::crypto::generateIdentity(), true);

    node.setOnStatusChanged([](dht::NodeStatus statV4, dht::NodeStatus statV6){
//        cout << "changed status: ";
//        cout << "v4 [" << statV4 << "] ";
//        cout << "v6 [" << statV6 << "] " << endl;
        if(previousV4Status != statV4){
            cout << statV4 << endl;
            previousV4Status = statV4;
        }
    });

    // Join the network through any running node,
    // here using a known bootstrap node.
    node.bootstrap("bootstrap.ring.cx", "4222");

    std::cout << "OpenDht node " << node.getNodeId() << " running on port " <<  node.getBoundPort() << std::endl;

//    scanNetwork();

    processInput();
    

    cout << "node joining" << endl;
    
    node.join();
    return 0;
}

void printCommands(){
    std::cout << "Commands:" << std::endl;
    std::cout << "\tget <key>           : try get (asynchronously) the given key value" << std::endl;
    std::cout << "\tput <key> <value>   : put a value with the given key" << std::endl;
    std::cout << "\taddr                : print public addresses" << std::endl;    
    std::cout << "\thelp                : print this instruction" << std::endl;    
    std::cout << "Press ENTER to exit" << std::endl;    
}

void handleCommand(std::stringstream & input);
void doGet(const std::string & key);
void doPut(const std::string & key, const std::string & value);
void printAddresses();

void processInput(){
    printCommands();
    
    std::string line;
    
    while(std::getline(std::cin, line) && !line.empty()){
        std::stringstream input(line);
        handleCommand(input);
    }
    
    std::cout << "Bye!" << std::endl;
}

void discardBlanks(std::istream& input)
{
    while(input.good() && isblank(input.peek())){
        input.get();
    }    
}

std::string readArg(std::stringstream& input){
    std::string quoted;
    
    discardBlanks(input);
    
    if(!input.good()){
        return string();
    }
    
    if(input.peek() == '"'){
        input.ignore(1);
        std::getline(input, quoted, '"');
    }
    else{
        input >> quoted;
    }
    
    return quoted;
}

void handleCommand(std::stringstream & input){
    std::string command, key, value;
    input >> command;

    std::transform(command.begin(), command.end(), command.begin(), ::tolower);

    if(command == "get"){
        key = readArg(input);
        doGet(key);
        return;
    }
    
    if(command == "put"){
//        input >> key >> value;
        key = readArg(input);
        value = readArg(input);
        
        doPut(key, value);
        return;
    }
    
    if(command == "addr"){
        printAddresses(node);
        return;
    }
    
    if(command != "help"){
        cout << "unknown command '" << command << "'" << endl;
    }
    printCommands();
    return;
}

struct StringContainer {
    std::string data;

    // implements msgpack-c serialization methods
    MSGPACK_DEFINE(data);
};


void doGet(const std::string & key){
    if(key.empty()){
        cout << "empty key" << endl;
        return;
    }

    cout << "Get(" << key  << ")" << endl;

    node.get(key, [key](const std::vector<std::shared_ptr<dht::Value>>& values) {
        cout << "got values for key(" << key << "): " << endl;
        
        for (const std::shared_ptr<dht::Value>& value : values){
            std::cout << "\t" << *value << " -> "; 
            if(value->user_type == "text/plain"){
                StringContainer container = value->unpack<StringContainer>();
                cout << container.data;
            }
            else{
                cout << " <unknown type>";
            }
            cout << std::endl;
        }
        return true; // return false to stop the search
    },
    [](bool success) {
        std::cout << "Get finished with " << (success ? "success" : "failure") << std::endl;
    });

}

void doPut(const std::string & key, const std::string & value){    
    if(key.empty()){
        cout << "empty key" << endl;
        return;
    }
    if(value.empty()){
        cout << "empty value" << endl;
        return;
    }
    cout << "Put(" << key << "," << value << ")" << endl;

    dht::Value dhtValue{StringContainer{value}};
    dhtValue.user_type = "text/plain";
    node.putSigned(key, std::move(dhtValue), [key](bool success){
        cout << "Put '" << key << "' " << (success ? "succeeded" : "failed") << endl;
    });
}


void printAddresses(){
    std::cout << "public addresses: " << std::endl;
                
    for(const auto & addr : node.getPublicAddressStr()){
        cout << addr << endl;
    }
}



void scanNetwork(){
    std::cout << "Scanning network..." << std::endl;
    auto all_nodes = std::make_shared<NodeSet>();

    dht::InfoHash cur_h {};
    cur_h.setBit(8*HASH_LEN-1, 1);

    std::this_thread::sleep_for(std::chrono::seconds(2));

    std::atomic_uint done {false};
    step(node, done, all_nodes, cur_h, 0);

    {
        std::mutex m;
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&](){
            return done.load() == 0;
        });
    }

    std::cout << std::endl << "Scan ended: " << all_nodes->size() << " nodes found." << std::endl;
    int count = 0;
    for (const auto& n : *all_nodes)
        std::cout << "Node " << ++count << ": " << *n << std::endl;

    cout << "public addresses: ";

    printAddresses(node);
}

void printAddresses(dht::DhtRunner & node)
{
    cout << "[";
    bool first = true;
    string host, port;

    for (const dht::SockAddr & addr : node.getPublicAddress()) {
        if(!first){
            cout << ", ";
        }
        
        cout << addr.toString();
//        addrToString(&addr.first, host, port);
//        cout << host << ":";
//        cout << port;

//        if(addr.first.ss_family == AF_INET){
//            const sockaddr_in * ipv4Addr = (const sockaddr_in *)&addr.first;
//            cout << ipv4Addr->sin_port;
//        }
//        cout << addr.toString();

        first = false;
    }
    cout << "]";
    
    cout << endl;
}

bool addrToString(const sockaddr_storage * addr, std::string & outHost, std::string & outPort){
    socklen_t addrSize = sizeof(struct sockaddr_storage);

    outHost.resize(NI_MAXHOST);
    outPort.resize(NI_MAXSERV);

    char * hostStr = &outHost[0];
    char * portStr = &outPort[0];

    int rc = getnameinfo((struct sockaddr *)addr, addrSize, hostStr, outHost.size(), portStr, outPort.size(), NI_NUMERICHOST | NI_NUMERICSERV);

    if(rc != 0){
        outHost.clear();
        outPort.clear();

        return false;
    }

    outHost.resize(strlen(hostStr));
    outPort.resize(strlen(portStr));

    return true;
}

void
step(dht::DhtRunner& dht, std::atomic_uint& done, std::shared_ptr<NodeSet> all_nodes, dht::InfoHash cur_h, unsigned cur_depth)
{
    std::cout << "step at " << cur_h << ", depth " << cur_depth << std::endl;
    done++;
    dht.get(cur_h, [all_nodes](const std::vector<std::shared_ptr<dht::Value>>& /*values*/) {
        return true;
    }, [&,all_nodes,cur_h,cur_depth](bool, const std::vector<std::shared_ptr<dht::Node>>& nodes) {
        all_nodes->insert(nodes.begin(), nodes.end());
        NodeSet sbuck {nodes.begin(), nodes.end()};
        if (not sbuck.empty()) {
            unsigned bdepth = sbuck.size()==1 ? 0u : dht::InfoHash::commonBits((*sbuck.begin())->id, (*std::prev(sbuck.end()))->id);
            unsigned target_depth = std::min(159u, bdepth+3u);
            std::cout << cur_h << " : " << nodes.size() << " nodes; target is " << target_depth << " bits deep (cur " << cur_depth << ")" << std::endl;
            for (unsigned b = cur_depth ; b < target_depth; b++) {
                auto new_h = cur_h;
                new_h.setBit(b, 1);
                step(dht, done, all_nodes, new_h, b+1);
            }
        }
        done--;
        std::cout << done.load() << " operations left, " << all_nodes->size() << " nodes found." << std::endl;
        cv.notify_one();
    });
}
