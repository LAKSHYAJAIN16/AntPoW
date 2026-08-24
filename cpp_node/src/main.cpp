// CLI entry point: wallet management, running a node, sending transactions,
// and querying a running node. Mirrors node/antchain_node/cli.py.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "chain.hpp"
#include "consensus.hpp"
#include "crypto.hpp"
#include "json.hpp"
#include "node.hpp"
#include "rpc.hpp"
#include "transaction.hpp"

using namespace antchain_node;

namespace {

struct Args {
    std::map<std::string, std::string> values;
    std::set<std::string> flags;

    bool has(const std::string& k) const { return values.count(k) || flags.count(k); }
    std::string get(const std::string& k, const std::string& def = "") const {
        auto it = values.find(k);
        return it == values.end() ? def : it->second;
    }
    double get_double(const std::string& k, double def) const {
        auto it = values.find(k);
        return it == values.end() ? def : std::stod(it->second);
    }
    int get_int(const std::string& k, int def) const {
        auto it = values.find(k);
        return it == values.end() ? def : std::stoi(it->second);
    }
};

// Boolean (no-value) flags, everything else is `--key value`.
Args parse_args(int argc, char** argv, int start, const std::set<std::string>& bool_flags) {
    Args a;
    for (int i = start; i < argc; ++i) {
        std::string tok = argv[i];
        if (tok.rfind("--", 0) != 0) continue;
        std::string key = tok.substr(2);
        if (bool_flags.count(key)) {
            a.flags.insert(key);
            continue;
        }
        if (i + 1 < argc) {
            a.values[key] = argv[++i];
        }
    }
    return a;
}

std::string read_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("could not open " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) throw std::runtime_error("could not write " + path);
    out << content;
}

Wallet load_wallet(const std::string& path) {
    Json j = Json::parse(read_file(path));
    return Wallet::from_seed_hex(j.at("seed_hex").as_string());
}

void save_wallet(const Wallet& w, const std::string& path) {
    Json j = Json::object();
    j.set("seed_hex", Json::string(w.seed_hex()));
    j.set("public_key_hex", Json::string(w.pubkey_hex()));
    j.set("address", Json::string(w.address()));
    write_file(path, j.dump());
}

std::vector<std::pair<std::string, int>> parse_peers(const std::string& peers_arg) {
    std::vector<std::pair<std::string, int>> out;
    if (peers_arg.empty()) return out;
    std::stringstream ss(peers_arg);
    std::string entry;
    while (std::getline(ss, entry, ',')) {
        if (entry.empty()) continue;
        size_t colon = entry.rfind(':');
        if (colon == std::string::npos) continue;
        out.emplace_back(entry.substr(0, colon), std::stoi(entry.substr(colon + 1)));
    }
    return out;
}

std::pair<std::string, int> split_host_port(const std::string& hp) {
    size_t colon = hp.rfind(':');
    if (colon == std::string::npos) throw std::runtime_error("expected host:port, got " + hp);
    return {hp.substr(0, colon), std::stoi(hp.substr(colon + 1))};
}

void print_usage() {
    std::cout <<
        "antchain-node <command> [options]\n\n"
        "Commands:\n"
        "  wallet-new --out FILE\n"
        "  wallet-address --wallet FILE\n"
        "  run --port P [--host H] [--peers H:P,...] [--data FILE] [--wallet FILE] [--mine]\n"
        "      [--cities N] [--t-opt-frac F] [--lam F] [--difficulty-bits N]\n"
        "      [--retarget-interval N] [--target-block-time F] [--block-reward F]\n"
        "  send --node H:P --wallet FILE --to ADDR --amount N [--nonce N]\n"
        "  balance --node H:P --address ADDR\n"
        "  status --node H:P\n";
}

int cmd_wallet_new(const Args& args) {
    if (!args.has("out")) { std::cerr << "error: --out is required\n"; return 1; }
    Wallet w = Wallet::generate();
    save_wallet(w, args.get("out"));
    std::cout << "Wrote wallet to " << args.get("out") << "\n";
    std::cout << "Address: " << w.address() << "\n";
    return 0;
}

int cmd_wallet_address(const Args& args) {
    if (!args.has("wallet")) { std::cerr << "error: --wallet is required\n"; return 1; }
    Wallet w = load_wallet(args.get("wallet"));
    std::cout << w.address() << "\n";
    return 0;
}

int cmd_run(const Args& args) {
    if (!args.has("port")) { std::cerr << "error: --port is required\n"; return 1; }
    ConsensusParams params;
    params.n_cities = args.get_int("cities", 10);
    params.t_opt_frac = args.get_double("t-opt-frac", 1.15);
    params.lam = args.get_double("lam", 3.0);
    params.difficulty_bits = args.get_int("difficulty-bits", 20);
    params.retarget_interval = args.get_int("retarget-interval", 10);
    params.target_block_time = args.get_double("target-block-time", 8.0);
    params.block_reward = args.get_double("block-reward", 50.0);

    bool mine = args.has("mine");
    std::string miner_address;
    if (mine) {
        if (!args.has("wallet")) {
            std::cerr << "error: --mine requires --wallet (block rewards need a recipient address)\n";
            return 1;
        }
        miner_address = load_wallet(args.get("wallet")).address();
    }

    Node node(args.get("host", "127.0.0.1"), args.get_int("port", 0), params, args.get("data", ""),
              miner_address, mine);
    auto peers = parse_peers(args.get("peers", ""));
    node.start(peers);
    std::cout << "[node] running on " << node.network.host << ":" << node.network.port
              << ", height=" << node.chain.height() << "\n";
    if (!miner_address.empty()) std::cout << "[node] mining to " << miner_address << "\n";
    node.run_forever();
    return 0;
}

int cmd_send(const Args& args) {
    if (!args.has("node") || !args.has("wallet") || !args.has("to") || !args.has("amount")) {
        std::cerr << "error: --node, --wallet, --to, --amount are required\n";
        return 1;
    }
    Wallet wallet = load_wallet(args.get("wallet"));
    auto [host, port] = split_host_port(args.get("node"));

    Json bal_req = Json::object();
    bal_req.set("type", Json::string("get_balance"));
    bal_req.set("address", Json::string(wallet.address()));
    Json bal_resp = rpc_request(host, port, bal_req);

    uint64_t nonce = args.has("nonce") ? static_cast<uint64_t>(args.get_int("nonce", 0))
                                        : static_cast<uint64_t>(bal_resp.get("nonce", Json::integer(0)).as_int());

    Transaction tx;
    tx.sender = wallet.address();
    tx.sender_pubkey = wallet.pubkey_hex();
    tx.recipient = args.get("to");
    tx.amount = args.get_double("amount", 0.0);
    tx.nonce = nonce;
    tx.sign(wallet);

    Json submit = Json::object();
    submit.set("type", Json::string("submit_tx"));
    submit.set("tx", tx.to_json());
    Json result = rpc_request(host, port, submit);
    if (result.has("accepted") && result.at("accepted").as_bool()) {
        std::cout << "Submitted tx " << tx.tx_hash() << "\n";
        return 0;
    }
    std::cerr << "Node rejected tx " << tx.tx_hash() << " (insufficient balance, bad nonce, or bad signature)\n";
    return 1;
}

int cmd_balance(const Args& args) {
    if (!args.has("node") || !args.has("address")) {
        std::cerr << "error: --node and --address are required\n";
        return 1;
    }
    auto [host, port] = split_host_port(args.get("node"));
    Json req = Json::object();
    req.set("type", Json::string("get_balance"));
    req.set("address", Json::string(args.get("address")));
    Json resp = rpc_request(host, port, req);
    std::cout << resp.at("balance").as_double() << "\n";
    return 0;
}

int cmd_status(const Args& args) {
    if (!args.has("node")) { std::cerr << "error: --node is required\n"; return 1; }
    auto [host, port] = split_host_port(args.get("node"));
    Json req = Json::object();
    req.set("type", Json::string("get_status"));
    Json resp = rpc_request(host, port, req);
    std::cout << resp.dump() << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_usage(); return 1; }
    std::string command = argv[1];
    try {
        if (command == "wallet-new") return cmd_wallet_new(parse_args(argc, argv, 2, {}));
        if (command == "wallet-address") return cmd_wallet_address(parse_args(argc, argv, 2, {}));
        if (command == "run") return cmd_run(parse_args(argc, argv, 2, {"mine"}));
        if (command == "send") return cmd_send(parse_args(argc, argv, 2, {}));
        if (command == "balance") return cmd_balance(parse_args(argc, argv, 2, {}));
        if (command == "status") return cmd_status(parse_args(argc, argv, 2, {}));
        if (command == "--help" || command == "-h") { print_usage(); return 0; }
        std::cerr << "unknown command: " << command << "\n";
        print_usage();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
