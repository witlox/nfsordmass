#!/bin/bash
# Setup kfabric headers as a local build dependency
# This script downloads kfabric without installing it system-wide

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
KFABRIC_DIR="$PROJECT_ROOT/external/kfabric"
KFABRIC_HEADERS="$PROJECT_ROOT/external/kfabric-headers"
KFABRIC_REPO="https://github.com/ofiwg/kfabric.git"

echo "Setting up kfabric as build dependency..."

# Create external directory if it doesn't exist
mkdir -p "$PROJECT_ROOT/external"

# Check if kfabric already exists
if [ -d "$KFABRIC_DIR" ]; then
    echo "kfabric directory already exists at $KFABRIC_DIR"
    read -p "Do you want to update it? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        cd "$KFABRIC_DIR"
        git pull
    fi
else
    echo "Cloning kfabric from $KFABRIC_REPO..."
    git clone "$KFABRIC_REPO" "$KFABRIC_DIR"
fi

# Verify kfabric headers exist
if [ ! -f "$KFABRIC_DIR/include/kfabric.h" ]; then
    echo "✗ Error: kfabric source not found"
    echo "  Expected: $KFABRIC_DIR/include/kfabric.h"
    exit 1
fi

# Create wrapper header structure if it doesn't exist
echo "Creating compatibility header structure..."
mkdir -p "$KFABRIC_HEADERS/rdma/kfi"

# Create wrapper headers that map rdma/kfi/* to kfi_*
cat > "$KFABRIC_HEADERS/rdma/kfi/fabric.h" << 'EOF'
/* Compatibility wrapper for kfabric */
#ifndef _RDMA_KFI_FABRIC_H
#define _RDMA_KFI_FABRIC_H
#include "kfabric.h"
#endif /* _RDMA_KFI_FABRIC_H */
EOF

cat > "$KFABRIC_HEADERS/rdma/kfi/endpoint.h" << 'EOF'
/* Compatibility wrapper for kfabric */
#ifndef _RDMA_KFI_ENDPOINT_H
#define _RDMA_KFI_ENDPOINT_H
#include "kfi_endpoint.h"
#endif /* _RDMA_KFI_ENDPOINT_H */
EOF

cat > "$KFABRIC_HEADERS/rdma/kfi/domain.h" << 'EOF'
/* Compatibility wrapper for kfabric */
#ifndef _RDMA_KFI_DOMAIN_H
#define _RDMA_KFI_DOMAIN_H
#include "kfi_domain.h"
#endif /* _RDMA_KFI_DOMAIN_H */
EOF

cat > "$KFABRIC_HEADERS/rdma/kfi/cq.h" << 'EOF'
/* Compatibility wrapper for kfabric */
#ifndef _RDMA_KFI_CQ_H
#define _RDMA_KFI_CQ_H
#include "kfi_eq.h"
#endif /* _RDMA_KFI_CQ_H */
EOF

cat > "$KFABRIC_HEADERS/rdma/kfi/mr.h" << 'EOF'
/* Compatibility wrapper for kfabric */
#ifndef _RDMA_KFI_MR_H
#define _RDMA_KFI_MR_H
#include "kfi_rma.h"
#endif /* _RDMA_KFI_MR_H */
EOF

echo "✓ kfabric headers found at $KFABRIC_DIR/include"
echo "✓ Compatibility headers created at $KFABRIC_HEADERS"
echo "✓ Build dependency setup complete"

# Add to .gitignore if not already there
GITIGNORE="$PROJECT_ROOT/.gitignore"
if ! grep -q "^external/" "$GITIGNORE" 2>/dev/null; then
    echo "external/" >> "$GITIGNORE"
    echo "✓ Added external/ to .gitignore"
fi

echo ""
echo "Header mapping:"
echo "  rdma/kfi/fabric.h   -> kfabric.h"
echo "  rdma/kfi/endpoint.h -> kfi_endpoint.h"
echo "  rdma/kfi/domain.h   -> kfi_domain.h"
echo "  rdma/kfi/cq.h       -> kfi_eq.h"
echo "  rdma/kfi/mr.h       -> kfi_rma.h"
echo ""
echo "Next steps:"
echo "  1. Run 'make' to build the kernel modules"
echo "  2. The build will use headers from: external/kfabric-headers and external/kfabric/include"
