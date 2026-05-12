#pragma once

#include "tokenizer.hpp"
#include "cnpy.h"

namespace tokenizer
{
    inline std::pair<std::vector<std::vector<float>>, std::vector<int>>
    get_hailo_input(const std::vector<std::string>& input_text,
                    const std::string& embedding_npy_path,
                    const std::string& bpe_vocab_path) {
        Tokenizer tokenizer(bpe_vocab_path);

        std::vector<std::vector<int>> tokens = tokenizer.tokenize(input_text);

        cnpy::NpyArray arr = cnpy::npy_load(embedding_npy_path);
        float* loaded_data = arr.data<float>();
        if (arr.shape.size() != 2)
            throw std::runtime_error("token embedding npy must be 2D [vocab_size, embedding_dim]");
        const int vocab_size    = static_cast<int>(arr.shape[0]);
        const int embedding_dim = static_cast<int>(arr.shape[1]);

        std::vector<float> embedding_weights(loaded_data, loaded_data + vocab_size * embedding_dim);

        int num_tokens = (int)tokens[0].size();

        std::vector<std::vector<float>> hailo_input(tokens.size(), std::vector<float>(num_tokens * embedding_dim));
        std::vector<int> last_tokens(tokens.size(), 0);

        for (size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].size() != (size_t)num_tokens) {
                throw std::invalid_argument("Each tokenized text must contain exactly 77 tokens.");
            }

            for (int j = 0; j < num_tokens; ++j) {
                int token_id = tokens[i][j];
                if (token_id < 0 || token_id >= vocab_size)
                    throw std::out_of_range("Token ID out of vocabulary range");
                if (token_id > 0) last_tokens[i]++;

                for (int k = 0; k < embedding_dim; ++k) {
                    hailo_input[i][j * embedding_dim + k] =
                        embedding_weights[token_id * embedding_dim + k];
                }
            }
        }

        return std::make_pair(hailo_input, last_tokens);
    }
} // namespace tokenizer
