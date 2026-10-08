// "Testar no jogo" (F5): roda o scripts/research/ring_deploy.py como processo filho e lê o progresso dele
// (core/progress.hpp) sem travar a janela. O filho fica num grupo de processos próprio: cancelar manda SIGTERM
// ao grupo (o script e a etapa que ele estiver rodando). O jogo é aberto pela Steam numa sessão à parte e
// não morre junto.
#pragma once

#include "core/progress.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace dr2::app {

class ChildProcess {
public:
    ChildProcess() = default;
    // Não mata o filho: fechar o editor durante a carga deixa o porte e o jogo seguirem (a câmera livre
    // ainda é ligada na largada). Só fecha o pipe; o script segue sem escrever.
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // Começa `argv` (argv[0] procurado no PATH) com stdout e stderr num pipe. false e `error` se não deu.
    bool start(const std::vector<std::string>& argv, std::string& error);
    // Lê o que chegou (sem bloquear) e passa cada linha completa a `progress`; quando o filho acaba, chama
    // progress.exited se ele não disse @done/@fail.
    void poll(Progress& progress);
    bool running() const { return pid_ > 0; }
    // SIGTERM ao grupo do filho (o poll vê a saída depois).
    void terminate();
    double seconds() const { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count(); }

private:
    int pid_ = -1;
    int fd_ = -1;
    int exit_code_pending_ = -1;  // o filho saiu antes de a saída acabar de chegar
    std::string partial_;
    std::chrono::steady_clock::time_point start_{};
};

// Raiz do repositório (onde fica scripts/research/ring_deploy.py): a pasta atual ou uma acima da pista aberta.
// Vazio se não achar.
std::string find_repo(const std::string& track_dir);

}  // namespace dr2::app
