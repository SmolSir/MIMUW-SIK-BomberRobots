#include <boost/program_options.hpp>

#include "structures.hpp"

using boost::asio::awaitable;
using boost::asio::use_awaitable;
using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::ip::udp;
using boost::asio::ip::tcp;

namespace po = boost::program_options;
namespace this_coro = boost::asio::this_coro;

#if defined(BOOST_ASIO_ENABLE_HANDLER_TRACKING)
# define use_awaitable \
  boost::asio::use_awaitable_t(__FILE__, __LINE__, __PRETTY_FUNCTION__)
#endif

/* structure storing address and port as strings */
struct sockaddr_str {
    string addr;
    string port;
};

string make_string(boost::asio::streambuf &streambuf) {
    return {buffers_begin(streambuf.data()), buffers_end(streambuf.data())};
}

void print_streambuf(boost::asio::streambuf &streambuf) {
    string content = {buffers_begin(streambuf.data()), buffers_end(streambuf.data())};
    cout << "buffer has " << content.size() << " byte(s)\n";
    cout << "ASCII:\n";
    for (auto c : content) cout << (int) c << "\t";
    cout << "\n";
    cout << "CHARS:\n";
    for (auto c : content) cout << (char) c << "\t";
    cout << "\n";
}

sockaddr_str get_sockaddr_str(string &str) {
    auto delimiter = str.find_last_of(':');
    sockaddr_str addr_str = {
        str.substr(0, delimiter),
        str.substr(delimiter + 1, str.size() - delimiter),
    };
    return addr_str;
}

bool process_command_line(int argc, char** argv, 
                          sockaddr_str &gui, 
                          sockaddr_str &srv, 
                          string &name, 
                          string &port) 
{
    string gui_str;
    string srv_str;
    uint16_t port_u16;

    try {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("gui-address,d", po::value<string>(&gui_str)->required(), "gui address:port")
            ("help,h", "help message")
            ("player-name,n", po::value<string>(&name)->required(), "player name")
            ("port,p", po::value<uint16_t>(&port_u16)->required(), "port for comms from gui")
            ("server-address,s", po::value<string>(&srv_str)->required(), "server address:port")
        ;

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help")) {
            cout << desc << "\n";
            return false;
        }

        po::notify(vm);
    }
    catch(exception &e) {
        cerr << "error: " << e.what() << "\n";
        return false;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
        return false;
    }

    gui = get_sockaddr_str(gui_str);
    srv = get_sockaddr_str(srv_str);
    port = to_string(port_u16);

    return true;
}

awaitable<void> gui_listener() {
    for (int i = 0; i < 10; i++) {
        boost::asio::deadline_timer timer(co_await boost::asio::this_coro::executor, boost::posix_time::seconds(2));
        co_await timer.async_wait(use_awaitable);
        cout << "gui_listener" << endl;
    }
}

awaitable<void> server_listener() {
    for (int i = 0; i < 4; i++) {
        boost::asio::deadline_timer timer(co_await boost::asio::this_coro::executor, boost::posix_time::seconds(5));
        co_await timer.async_wait(use_awaitable);
        cout << "server_listener" << endl;
    }
}

boost::asio::streambuf UDP_buffer;

void read_UDP(void *arg, size_t size) {
    if (UDP_buffer.size() < size) {
        throw length_error("unexpected EOF in read_UDP from UDP_buffer\n");
    }
    UDP_buffer.sgetn((char *) arg, size);
}

void read_TCP(void *arg, size_t size) {

}

int main(int argc, char *argv[]) {

    sockaddr_str gui;
    sockaddr_str srv;
    string player_name;
    string port;

    /* parsing command line parameters */
    bool parsing_result = process_command_line(argc, argv, gui, srv, player_name, port);

    if (!parsing_result) {
        cerr << "Failed to parse parameters.\n";
        return 1;
    }

    cout << gui.addr << ":" << gui.port << "    "
         << srv.addr << ":" << srv.port << "    "
         << player_name << "    "
         << port << "\n";

    try {
        boost::asio::io_context io_context;
        
        udp::resolver gui_resolver(io_context);
        udp::endpoint gui_endpoint = *gui_resolver.resolve(gui.addr, gui.port).begin();
        udp::socket gui_socket(io_context);
        gui_socket.open(gui_endpoint.protocol());

        tcp::resolver srv_resolver(io_context);
        tcp::resolver::results_type srv_endpoints = srv_resolver.resolve(srv.addr, srv.port);
        tcp::socket srv_socket(io_context);
//        boost::asio::connect(srv_socket, srv_endpoints);

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){ io_context.stop(); });

        // TEST 1
        // boost::asio::streambuf buf;
        // uint8_t message1 = 8;
        // uint16_t message2 = 16;
        // uint32_t message3 = 32;
        // string message4 = "256";
        // vector<uint8_t> message5 = {89, 69, 83};
        // uint16_t one = 1, two = 2;
        // map<uint16_t, string> message6 = {{one, "Ola"}, {two, "Bart"}};
        // ClientMessageServer message7 = Move {Direction::Up};

        // serialize(message1, buf);
        // serialize(message2, buf);
        // serialize(message3, buf);
        // serialize(message4, buf);
        // serialize(message5, buf);
        // serialize(message6, buf);
        // serialize(message7, buf);
        
        // print_streambuf(buf);

        // TEST 2
        // boost::asio::streambuf buf2;
        // ClientMessageGui lobby = Lobby {
        //     .server_name = "Hello, world!",
        //     .players_count = 1,
        //     .size_x = 10,
        //     .size_y = 10,
        //     .game_length = 100,
        //     .explosion_radius = 5,
        //     .bomb_timer = 20,
        //     .players = {{1, {"SmolSir", "127.0.0.1:10022"}}}
        // };
        // serialize(lobby, buf2);
        // gui_socket.send_to(buf2.data(), gui_endpoint);

        // sleep(5);

        // TEST 3
        // boost::asio::streambuf buf3;
        // ClientMessageGui game = Game {
        //     .server_name = "Hello, world!",
        //     .size_x = 10,
        //     .size_y = 10,
        //     .game_length = 100,
        //     .turn = 5,
        //     .players = {{1, {"SmolSir", "127.0.0.1:10022"}}},
        //     .player_positions = {{1, {5, 5}}},
        //     .blocks = {{1, 1}},
        //     .bombs = {{{2, 2}, 100}},
        //     .explosions = {{3, 3}},
        //     .scores = {{1, 42}}
        // };
        // serialize(game, buf3);
        // print_streambuf(buf3);
        // gui_socket.send_to(buf3.data(), gui_endpoint);

        // TEST 4
        // boost::asio::streambuf buf4;
        // ClientMessageGui game = Game {
        //     .server_name = "Hello, world!",
        //     .size_x = 7,
        //     .size_y = 7,
        //     .game_length = 9,
        //     .turn = 6,
        //     .players = {{1, {"SmolSir", "127.0.0.1:10022"}}},
        //     .player_positions = {{1, {3, 4}}},
        //     .blocks = {{3, 1}, {3, 2}, {3, 3}},
        //     .bombs = {{{2, 1}, 1}, {{4, 1}, 1}},
        //     .explosions = {{3, 5}},
        //     .scores = {{1, 42}}
        // };
        // serialize(game, buf4);
        // print_streambuf(buf4);
        // gui_socket.send_to(buf4.data(), gui_endpoint);

        // TEST 5
        // uint8_t i_8 = 8;
        // uint16_t i_16 = 16;
        // uint32_t i_32 = 32;
        // serialize(i_8, UDP_buffer);
        // serialize(i_16, UDP_buffer);
        // serialize(i_32, UDP_buffer);

        // print_streambuf(UDP_buffer);

        // uint8_t i_8_des;
        // uint16_t i_16_des;
        // uint32_t i_32_des;

        // deserialize(i_8_des, read_UDP);
        // cout << "result 8 is: " << (int) i_8_des << "\n";

        // deserialize(i_16_des, read_UDP);
        // cout << "result 16 is: " << (int) i_16_des << "\n";

        // deserialize(i_32_des, read_UDP);
        // cout << "result 32 is: " << (int) i_32_des << "\n";


        // TEST 6
        // vector<uint8_t> vec_8 = {8, 88};
        // serialize(vec_8, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // vector<string> vec = {"Ola", "Bart"};
        // serialize(vec, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // vector<uint8_t> vec_8_des;
        // deserialize(vec_8_des, read_UDP);
        // for (auto elem : vec_8_des) cout << (int) elem << ",\t";
        // cout << "\n";

        // vector<string> vec_des;
        // deserialize(vec_des, read_UDP);
        // for (auto elem : vec_des) cout << elem << ",\t";
        // cout << "\n";

        // TEST 7
        // string str = "Ola";
        // serialize(str, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // string str_des;
        // deserialize(str_des, read_UDP);
        // cout << "result is: " << str_des << "\n";

        // Direction direction = Direction::Up;
        // serialize(direction, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // Direction direction_des;
        // deserialize(direction_des, read_UDP);
        // cout << "result is: " << (int) direction_des << "\n";

        // TEST 8
        // map<string, uint32_t> map_src = {{"Ola", 1}, {"Bart", 42}};
        // serialize(map_src, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // map<string, uint32_t> map_des = {};
        // deserialize(map_des, read_UDP);
        // for (auto [key, value] : map_des) cout << "{ " << key << ", " << value << " }\t";
        // cout << "\n";

        // TEST 9
        // variant<uint8_t, uint16_t> var1 = (uint8_t) 8;
        // serialize(var1, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // variant<uint8_t, uint16_t> var1_des;
        // deserialize(var1_des, read_UDP);
        // cout << "result is: " << (int) get<uint8_t>(var1_des) << "\n";

        // variant<string, Direction> var2 = "Ola";
        // serialize(var2, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // variant<string, Direction> var2_des;
        // deserialize(var2_des, read_UDP);
        // cout << "result is: " << get<string>(var2_des) << "\n";

        // variant<string, Direction> var3 = Direction::Up;
        // serialize(var3, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // variant<string, Direction> var3_des;
        // deserialize(var3_des, read_UDP);
        // cout << "result is: " << (int) get<Direction>(var3_des) << "\n";

        // TEST 10
        // ClientMessageGui game = Game {
        //     .server_name = "Hello, world!",
        //     .size_x = 7,
        //     .size_y = 7,
        //     .game_length = 9,
        //     .turn = 6,
        //     .players = {{1, {"SmolSir", "127.0.0.1:10022"}}},
        //     .player_positions = {{1, {3, 4}}},
        //     .blocks = {{3, 1}, {3, 2}, {3, 3}},
        //     .bombs = {{{2, 1}, 1}, {{4, 1}, 1}},
        //     .explosions = {{3, 5}},
        //     .scores = {{1, 42}}
        // };

        // serialize(game, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // ClientMessageGui game_des;
        // deserialize(game_des, read_UDP);
        // assert(game == game_des);



        // co_spawn(io_context, gui_listener(), detached);
        // co_spawn(io_context, server_listener(), detached);

        io_context.run();
    }
    catch (exception &e) {
        cerr << "error: " << e.what() << "\n";
        return 1;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
        return 1;
    }

    return 0;
}