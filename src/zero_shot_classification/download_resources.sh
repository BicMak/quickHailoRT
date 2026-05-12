#!/bin/bash
#
# Downloads the public CLIP ViT-B/16 resources for Hailo8.
#
# .npy files (token_embedding, text_projection) must be extracted manually
# from OpenAI CLIP — see README.md.

set -e
cd "$(dirname "$0")"

# HEF — Hailo Model Zoo
mkdir -p hef
wget -P hef https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.18.0/hailo8/clip_vit_b_16_text_encoder.hef
wget -P hef https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.18.0/hailo8/clip_vit_b_16_image_encoder.hef

# BPE vocab — Hailo CS Data
mkdir -p tokenizer
wget -P tokenizer https://hailo-csdata.s3.eu-west-2.amazonaws.com/resources/txt+files/bpe_simple_vocab_16e6.txt

echo
echo "Done. Remember to place the following .npy files under npy/ :"
echo "  - clip_vit_b_16_token_embedding.npy"
echo "  - clip_vit_b_16_text_projection.npy"
echo "(see README.md for extraction instructions)"
