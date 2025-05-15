# Kubernetes Deployment and Service End-to-End Example

This document details the process and interactions between key Kubernetes components when applying a Deployment and a Service definition using `kubectl`.

We'll use the following two YAML files:

**`my-app-deployment.yaml`**

```yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: my-app
spec:
  replicas: 3
  selector:
    matchLabels:
      app: my-app
  template:
    metadata:
      labels:
        app: my-app
    spec:
      containers:
      - name: my-app-container
        image: your-image:latest
        ports:
        - containerPort: 8000
```

**`my-app-service.yaml`**


```yaml
apiVersion: v1
kind: Service
metadata:
  name: my-app-service
spec:
  selector:
    app: my-app
  ports:
  - protocol: TCP
    port: 80
    targetPort: 8000
  type: ClusterIP

```

Here's the sequence of events when you apply these files using `kubectl`:

### Phase 1: Applying the Deployment

User runs `kubectl apply -f my-app-deployment.yaml`:

1.  The user initiates the process from their terminal.
2.  `kubectl` (the client-side tool) parses the `my-app-deployment.yaml` file.
3.  **kubectl sends API Request to Kube-apiserver:**
    * `kubectl` constructs an HTTP POST request to the Kubernetes API server's `/apis/apps/v1/namespaces/<namespace>/deployments` endpoint (assuming a namespace is used).
    * This request contains the definition of the `Deployment` object.
4.  **Kube-apiserver receives and authenticates/authorizes the request:**
    * The **Kube-apiserver** is the entry point to the cluster. It receives the request.
    * It performs Authentication to verify the identity of the user (based on `kubectl`'s configuration, like a kubeconfig file).
    * It performs Authorization to check if the authenticated user has permissions to create `Deployment` objects in the specified namespace.
5.  **Kube-apiserver Validation and Admission Control:**
    * The **Kube-apiserver** validates the `Deployment` object definition against the Kubernetes API schema.
    * It runs **Admission Controllers**. These are plugins that can intercept requests to the API server before the object is persisted. They can mutate the object (e.g., add default values) or validate it further. For a `Deployment`, admission controllers ensure the request is well-formed and adheres to cluster policies.
6.  **Kube-apiserver persists the Deployment object in etcd:**
    * If the request passes authentication, authorization, validation, and admission control, the **Kube-apiserver** serializes the `Deployment` object and stores it in **etcd**, the cluster's distributed key-value store. **etcd** is the single source of truth for the cluster's state.
7.  **Deployment Controller (within Kube-controller-manager) detects the new Deployment:**
    * The **Kube-controller-manager** runs various controllers as control loops that watch the state of the cluster through the **Kube-apiserver**.
    * The **Deployment Controller** specifically watches for new or updated `Deployment` objects in **etcd** (via the **Kube-apiserver**'s watch API).
    * It sees the newly created `my-app` `Deployment`.
8.  **Deployment Controller creates a ReplicaSet:**
    * Based on the `Deployment`'s `spec.replicas` (set to 3) and `spec.selector`/`spec.template`, the **Deployment Controller** determines that a `ReplicaSet` is needed to manage the desired number of Pods.
    * It creates a corresponding `ReplicaSet` object with the same label selector and Pod template. This request is sent back to the **Kube-apiserver**, which validates and persists it in **etcd**.
9.  **ReplicaSet Controller (within Kube-controller-manager) detects the new ReplicaSet:**
    * The **ReplicaSet Controller** watches for new or updated `ReplicaSet` objects.
    * It sees the newly created `ReplicaSet` for `my-app`.
10. **ReplicaSet Controller creates Pods:**
    * Based on the `ReplicaSet`'s `spec.replicas` (set to 3) and the Pod template, the **ReplicaSet Controller** sees that 3 Pods are desired but none exist yet.
    * It creates 3 `Pod` objects based on the Pod template defined in the `ReplicaSet`. These requests are sent to the **Kube-apiserver**, which validates and persists them in **etcd**. At this stage, the Pods exist as objects in **etcd** but are not yet running on a node and do not have an IP address assigned. Their `status.phase` will likely be `Pending`.

### Phase 2: Pod Scheduling

11. **Kube-scheduler detects new, unscheduled Pods:**
    * The **Kube-scheduler** watches the **Kube-apiserver** for new Pods that do not have `spec.nodeName` set (meaning they haven't been scheduled to a node yet).
    * It discovers the 3 `my-app` Pods in the `Pending` state.
12. **Kube-scheduler binds Pods to Nodes:**
    * For each pending Pod, the scheduler determines the best node to run it on based on various factors (resource requirements, node taints and tolerations, node affinity/anti-affinity, etc.).
    * Once a suitable node is found, the scheduler updates the `Pod` object in **etcd** (via the **Kube-apiserver**) by setting the `spec.nodeName` field to the name of the selected node. This is called "binding" the Pod to a node.

### Phase 3: Pod Creation and Networking on the Node

13. **Kubelet on the assigned Node detects the Pod:**
    * The **Kubelet** is an agent that runs on each worker node. It watches the **Kube-apiserver** for Pods that have been scheduled to its specific node (`spec.nodeName` matches its own name).
    * The **Kubelet** on the chosen node sees that a `my-app` Pod has been scheduled to it.
14. **Kubelet interacts with the Container Runtime:**
    * The **Kubelet** interacts with the **Container Runtime Interface (CRI)**, which communicates with the **Container Runtime** (like containerd or CRI-O) installed on the node.
    * The **Kubelet** instructs the **Container Runtime** to create the Pod's sandbox (which includes setting up the network namespace) and then create the application container(s) defined in the Pod's specification (in this case, the `my-app-container`).
15. **CNI Plugin assigns Pod IP and configures network:**
    * As part of the Pod sandbox creation, the **Container Runtime** calls the **Container Network Interface (CNI)** plugin configured on the node.
    * The **CNI** plugin's IP Address Management (**IPAM**) component allocates a unique IP address to the Pod from the range assigned for Pods on this node (either a dedicated node subnet or directly from the VPC/VNet subnet depending on the **CNI**).
    * The **CNI** plugin configures the network namespace for the Pod, setting up network interfaces and routes so the Pod can communicate within the cluster.
    * The **CNI** plugin reports the assigned Pod IP back to the **Container Runtime** and then the **Kubelet**.
16. **Kubelet updates Pod status:**
    * The **Kubelet** updates the Pod's status in **etcd** (via the **Kube-apiserver**) to reflect its current state, including its assigned IP address and that the container(s) are running (`status.phase` becomes `Running`).

This process (steps 12-15) repeats for all 3 Pods scheduled to various nodes in the cluster.

### Phase 4: Applying the Service

User runs `kubectl apply -f my-app-service.yaml`:

17. Similar to the `Deployment`, `kubectl` parses the Service YAML.
18. **kubectl sends API Request to Kube-apiserver:**
    * `kubectl` constructs an HTTP POST request to the **Kube-apiserver**'s `/api/v1/namespaces/<namespace>/services` endpoint.
19. **Kube-apiserver receives and authenticates/authorizes the request:**
    * The **Kube-apiserver** performs authentication and authorization for the Service creation request.
20. **Kube-apiserver Validation and Admission Control (including ClusterIP allocation):**
    * The **Kube-apiserver** validates the `Service` object definition.
    * Crucially, for a `Service` of type `ClusterIP`, an **admission controller** (or the API server's internal logic) is responsible for selecting an available IP address from the cluster's Service ClusterIP range (`--service-cluster-ip-range`).
    * This allocated IP is assigned to the `spec.clusterIP` field of the `Service` object.
21. **Kube-apiserver persists the Service object in etcd:**
    * The validated `Service` object, now with its assigned `ClusterIP`, is stored in **etcd** by the **Kube-apiserver**.
22. **Endpoint Controller (within Kube-controller-manager) detects the new Service:**
    * The **Endpoint Controller** watches for new or updated `Service` objects.
    * It sees the newly created `my-app-service`.
23. **Endpoint Controller identifies and tracks backing Pods:**
    * The **Endpoint Controller** evaluates the `Service`'s `spec.selector` (`app: my-app`).
    * It queries the **Kube-apiserver** to find all Pods that match this selector and are in a `Running` state with an assigned IP address.
    * It creates and continuously updates an `EndpointSlice` (or `Endpoint`) object in **etcd** (via the **Kube-apiserver**) that lists the IP addresses and ports of the healthy, matching Pods.
24. **Kube-proxy on each Node detects the Service and Endpoints:**
    * The **Kube-proxy** on each node watches the **Kube-apiserver** for changes to `Service` and `EndpointSlice` objects.
    * It learns about the `my-app-service`, its `ClusterIP` (e.g., `10.100.251.76`), port (`80`), target port (`8000`), and the IP addresses and target ports of the 3 healthy `my-app` Pods from the `EndpointSlice`.
25. **Kube-proxy programs network rules:**
    * Using the information from the `Service` and `EndpointSlice`, **Kube-proxy** configures the node's network stack (typically using `iptables` or `IPVS` rules).
    * These rules intercept traffic destined for the `Service`'s `ClusterIP` and port (e.g., `10.100.251.76:80`).
    * The rules are set up to load balance this traffic across the IP addresses and target ports of the healthy `my-app` Pods (e.g., `192.168.107.111:8000` and the IPs/ports of the other two pods).

### Phase 5: Accessing the Application

26. **Client within the cluster accesses the Service:**
    * Another Pod or component within the cluster tries to connect to `my-app-service:80`.
27. **Kubernetes DNS (CoreDNS) resolves the Service name:**
    * Kubernetes **DNS** (usually **CoreDNS**) resolves the Service name `my-app-service` to the Service's `ClusterIP` (e.g., `10.100.251.76`).
28. **Traffic is directed to the ClusterIP and intercepted by Kube-proxy:**
    * The traffic is directed towards the Service's `ClusterIP`.
    * The network rules programmed by **Kube-proxy** on the originating node intercept the traffic.
29. **Kube-proxy's rules load balance and forward traffic to a Pod:**
    * **Kube-proxy**'s rules select one of the healthy `my-app` Pod IPs (e.g., `192.168.107.111`) based on its load balancing mode.
    * It rewrites the destination address and port to the selected Pod's IP and target port (e.g., `192.168.107.111:8000`).
    * The traffic is then forwarded to the selected Pod, and the application running in the container on port `8000` receives the request.

This entire process, from applying the YAML to traffic reaching a Pod via the Service, happens very quickly in a healthy Kubernetes cluster, providing the desired abstraction and reliability.
```