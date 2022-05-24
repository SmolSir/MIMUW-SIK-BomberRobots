#include <boost/program_options.hpp>

#include "structures.hpp"

using boost::asio::co_spawn;
using boost::asio::detached;
using boost::asio::ip::udp;
using boost::asio::ip::tcp;

namespace po = boost::program_options;
namespace this_coro = boost::asio::this_coro;

#define MAX_UDP_DATA_SIZE 65527 // because of IPv6

/* -------------------------------------------------------------------------
   Useful templates and structures
   ------------------------------------------------------------------------- */
/* helper template for variant::visit */
template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

/* structure storing data about game status */
struct clientStatus {
    bool server_connected = false;
    bool in_lobby = false;
    bool in_game = false;
    bool join_request_sent = false;
};
/* structure storing address and port as strings */
struct sockaddrStr {
    string addr;
    string port;
};

/* -------------------------------------------------------------------------
   Global variables for less argument passing
   ------------------------------------------------------------------------- */
po::variables_map program_params;
boost::asio::streambuf UDP_buffer;

clientStatus status;
Hello settings;
Lobby lobby;
Game game;

sockaddrStr gui;
sockaddrStr server;
string player_name;
string port;

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

sockaddrStr get_sockaddr_str(string &str) {
    auto delimiter = str.find_last_of(':');
    sockaddrStr addr_str = {
        str.substr(0, delimiter),
        str.substr(delimiter + 1, str.size() - delimiter),
    };
    return addr_str;
}

bool process_command_line(int argc, char** argv) {
    string gui_sockaddr_str;
    string server_sockaddr_str;
    uint16_t port_u16;

    try {
        po::options_description desc("Allowed options");
        desc.add_options()
            ("gui-address,d", po::value<string>(&gui_sockaddr_str)->required(), "gui address:port")
            ("help,h", "help message")
            ("player-name,n", po::value<string>(&player_name)->required(), "player name")
            ("port,p", po::value<uint16_t>(&port_u16)->required(), "port for comms from gui")
            ("server-address,s", po::value<string>(&server_sockaddr_str)->required(), "server address:port")
        ;

        po::store(po::parse_command_line(argc, argv, desc), program_params);

        if (program_params.count("help")) {
            cout << desc << "\n";
            return false;
        }

        po::notify(program_params);
    }
    catch(exception &e) {
        cerr << "error: " << e.what() << "\n";
        return false;
    }
    catch(...) {
        cerr << "Exception of unknown type!\n";
        return false;
    }

    gui = get_sockaddr_str(gui_sockaddr_str);
    server = get_sockaddr_str(server_sockaddr_str);
    port = to_string(port_u16);

    return true;
}

void setup_client() {
    lobby.server_name = settings.server_name;
    lobby.players_count = settings.players_count;
    lobby.size_x = settings.size_x;
    lobby.size_y = settings.size_y;
    lobby.game_length = settings.game_length;
    lobby.explosion_radius = settings.explosion_radius;
    lobby.bomb_timer = settings.bomb_timer;
    lobby.players = { };

    game.server_name = settings.server_name;
    game.size_x = settings.size_x;
    game.size_y = settings.size_y;
    game.game_length = settings.game_length;
    game.turn = 0;
    game.players = { };
    game.player_positions = { };
    game.blocks = { };
    game.bombs = { };
    game.explosions = { };
    game.scores = { };
}

void accept_player(AcceptedPlayer &accepted_player) {
    lobby.players[accepted_player.id] = accepted_player.player;
    game.players[accepted_player.id] = accepted_player.player;
    game.scores[accepted_player.id] = (Score) 0;
}

awaitable<void> gui_listener(tcp::socket &server_socket, udp::socket &client_socket) {
    boost::asio::streambuf read_streambuf;
    boost::asio::streambuf send_streambuf;

    auto read_UDP = [&](void* arg, size_t size) -> awaitable<void> {
        try {
            read_streambuf.commit(size);
            read_streambuf.sgetn((char *) arg, size);
        }
        catch(exception &e) {
            throw e;
        }
        co_return;
    };

    for (;;) {
        /* ensure both streambufs are empty before operating on them */
        read_streambuf.consume(read_streambuf.size());
        send_streambuf.consume(send_streambuf.size());
        GuiMessageClient gui_message;
        try {
            boost::asio::streambuf::mutable_buffers_type mut_read_streambuf = read_streambuf.prepare(MAX_UDP_DATA_SIZE);
            size_t read_size = co_await client_socket.async_receive(mut_read_streambuf, use_awaitable);
            cout << "read " << read_size << " bytes from UDP\n";
            co_await deserialize(gui_message, read_UDP);
        }
        catch(exception &e) {
            cerr << "error: " << e.what() << "\n";
            continue;
        }
        cout << "No exceptions!\n";

        
        if (status.in_lobby && !status.join_request_sent) {
            ClientMessageServer join = Join { .name = player_name };
            try {
                serialize(join, send_streambuf);
                co_await server_socket.async_send(send_streambuf.data(), use_awaitable);
                status.join_request_sent = true;
            }
            catch(exception &e) {
                cerr << "error: " << e.what() << "\n";
                status.server_connected = false;
            }
        }
        
        ClientMessageServer client_message;
        if (status.in_game) {
            visit(overloaded {
                [&](PlaceBomb) {
                    cout << "received PlaceBomb\n";
                    client_message = PlaceBomb { };
                },
                [&](PlaceBlock) {
                    cout << "received PlaceBlock\n";
                    client_message = PlaceBlock { };
                },
                [&](Move message) {
                    cout << "received Move: " << (int) message.direction << "\n";
                    client_message = Move { .direction = message.direction };
                }
            }, gui_message);
            try {
                serialize(client_message, send_streambuf);
                co_await server_socket.async_send(send_streambuf.data(), use_awaitable);
            }
            catch(exception &e) {
                cerr << "error: " << e.what() << "\n";
                status.server_connected = false;
            }
        }
    }

    co_return;
}

awaitable<void> server_listener(tcp::socket &server_socket, udp::socket &gui_socket, udp::endpoint &gui_endpoint) {
    boost::asio::streambuf read_streambuf;
    boost::asio::streambuf send_streambuf;

    auto read_TCP = [&](void* arg, size_t size) -> awaitable<void> {
        try {
            cout << "reading " << size << " bytes from TCP\n";
            size_t read_size = co_await boost::asio::async_read(
                    server_socket, 
                    read_streambuf, 
                    boost::asio::transfer_exactly(size), 
                    use_awaitable
                );
            cout << "received " << read_size << " bytes from TCP:\n";
            print_streambuf(read_streambuf);
            read_streambuf.sgetn((char *) arg, size);
        }
        catch(exception &e) {
            status.server_connected = false;
            throw e;
        }
    };

    for (;;) {
        read_streambuf.consume(read_streambuf.size());
        send_streambuf.consume(send_streambuf.size());
        ServerMessageClient server_message;

        try {
            co_await deserialize(server_message, read_TCP);
        }
        catch(exception &e) {
            status.server_connected = false;
            break;
        }

        ClientMessageGui client_message;
        bool send_message = false;
        visit(overloaded {
            [&](Hello message) {
                cout << "\n*** received Hello over TCP ***\n";
                if (!status.in_lobby && !status.in_game) {
                    settings = message;
                    setup_client();
                    client_message = lobby;
                    send_message = true;
                    status.in_lobby = true;
                }
            },
            [&](AcceptedPlayer message) {
                cout << "\n*** received AcceptedPlayer over TCP ***\n";
                if (status.in_lobby) {
                    accept_player(message);
                    client_message = lobby;
                    send_message = true;
                }
            },
            [&](GameStarted message) {
                cout << "\n*** received gameStarted over TCP ***\n";
                if (!status.in_game) {
                    for (auto &[id, player] : message.players) {
                        AcceptedPlayer new_player = {id, player};
                        accept_player(new_player);
                    }
                    status.in_lobby = false;
                    status.in_game = true;
                }
            },
            [&](Turn message) {
                cout << "\n*** received Turn over TCP ***\n";
                if (status.in_game) {

                }
            },
            [&](GameEnded message) {
                cout << "\n*** received GameEnded over TCP ***\n";
                if (status.in_game) {
                    setup_client();
                    client_message = lobby;
                    send_message = true;
                    status.in_lobby = true;
                    status.in_game = false;
                    status.join_request_sent = false; // to allow client to join again
                }
            }
        }, server_message);

        try {
            if (send_message) {
                serialize(client_message, send_streambuf);
                co_await gui_socket.async_send_to(send_streambuf.data(), gui_endpoint, use_awaitable);
            }
        }
        catch(exception &e) {
            cerr << "error: " << e.what() << "\n";
            continue;
        }
    }

    co_return;
}

int main(int argc, char *argv[]) {

    /* parsing command line parameters */
    bool parsing_result = process_command_line(argc, argv);

    if (!parsing_result) {
        cerr << "Failed to parse parameters.\n";
        return 1;
    }

    cout << gui.addr << " : " << gui.port << "\t"
         << server.addr << " : " << server.port << "\t"
         << player_name << "\t"
         << port << "\n";

    try {
        boost::asio::io_context io_context;
        
        /* Prepare socket for receiving data from gui */
        udp::socket client_socket(io_context);
        client_socket.open(boost::asio::ip::udp::v6());
        client_socket.bind({boost::asio::ip::udp::v6(), program_params["port"].as<uint16_t>()});

        /* Prepare socket & endpoint for sending data to gui */
        udp::resolver gui_resolver(io_context);
        udp::endpoint gui_endpoint = *gui_resolver.resolve(gui.addr, gui.port).begin();
        udp::socket gui_socket(io_context);
        gui_socket.open(gui_endpoint.protocol());

        /* Prepare socket for exchanging data with server */
        tcp::resolver server_resolver(io_context);
        tcp::resolver::results_type server_endpoints = server_resolver.resolve(server.addr, server.port);
        tcp::socket server_socket(io_context);
        boost::asio::connect(server_socket, server_endpoints);
        server_socket.set_option(tcp::no_delay(true)); // set the TCP_NODELAY flag
        status.server_connected = true;

        boost::asio::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&](auto, auto){ io_context.stop(); });

        co_spawn(io_context, gui_listener(server_socket, client_socket), detached);
        co_spawn(io_context, server_listener(server_socket, gui_socket, gui_endpoint), detached);

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
        // co_await deserialize(var1_des, read_UDP);
        // cout << "\nresult is: " << (int) get<uint8_t>(var1_des) << "\n\n\n";

        // variant<string, Direction> var2 = "Ola";
        // serialize(var2, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // variant<string, Direction> var2_des;
        // co_await deserialize(var2_des, read_UDP);
        // cout << "result is: " << get<string>(var2_des) << "\n";

        // GuiMessageClient var3 = (Move) Direction::Down;
        // serialize(var3, UDP_buffer);
        // print_streambuf(UDP_buffer);

        // GuiMessageClient var3_des;
        // co_await deserialize(var3_des, read_UDP);
        // cout << "result is: " << (int) get<2>(var3_des).direction << "\n";

        //TEST 10
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
        // co_await deserialize(game_des, read_UDP);
        // assert(get<Game>(game) == get<Game>(game_des));
        // cout << "SUCCESFULLY TESTED\n";
        // co_return;

