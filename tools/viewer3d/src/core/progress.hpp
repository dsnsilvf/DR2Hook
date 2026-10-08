// Progresso de um processo filho lido linha a linha (protocolo do scripts/research/ring_deploy.py):
//
//   @step <k> <n> <texto>   etapa k de n começou
//   @progress <0..1>        andamento total
//   @done <texto>           terminou bem
//   @fail <texto>           parou com erro
//
// Qualquer outra linha é log. Sem SDL nem GL: o "Testar no jogo" (F5) do viewer usa, os testes também.
#pragma once

#include <cstddef>
#include <deque>
#include <string>

namespace dr2 {

struct Progress {
    enum class State { Running, Done, Failed };
    State state = State::Running;
    int step = 0, steps = 0;  // etapa atual (1..steps); 0 = nenhuma ainda
    std::string label;        // texto da etapa atual
    float fraction = 0.0f;    // 0..1, nunca volta
    std::string result;       // texto do @done ou do @fail
    std::deque<std::string> log;
    std::size_t max_log = 500;

    // Uma linha sem o '\n' (um '\r' no fim é ignorado).
    void feed(std::string line);
    // O processo acabou sem @done nem @fail: vira falha com o código de saída.
    void exited(int code);
};

}  // namespace dr2
