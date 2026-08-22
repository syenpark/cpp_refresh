
# Kubernetes Job Lab

This directory contains a small Kubernetes `Job` that runs to completion after
printing start and finish messages. It is useful for practicing basic Job
creation, status inspection, and cleanup with the local kind cluster.

## Prerequisites

- A running Kubernetes cluster
- `kubectl` configured for that cluster

The repository's kind cluster uses the `kind-mle-lab` context. Select it before
running the commands below if needed:

```bash
kubectl config use-context kind-mle-lab
```

## Run the Job

The checked-in manifest is [`job.yaml`](./job.yaml). Apply it and wait for the
Job to complete:

```bash
kubectl apply -f job.yaml
kubectl wait --for=condition=complete job/finite-job --timeout=60s
```

Inspect the Job and read its pod logs:

```bash
kubectl get job finite-job
kubectl logs job/finite-job
```

Expected output:

```text
job started
job finished
```

Remove the completed Job when finished:

```bash
kubectl delete -f job.yaml
```

## Regenerate the Manifest

To recreate `job.yaml` from the command line:

```bash
kubectl create job finite-job \
  --image=busybox:1.36 \
  --dry-run=client \
  -o yaml \
  -- sh -c 'echo "job started"; sleep 5; echo "job finished"' \
  > job.yaml
```
