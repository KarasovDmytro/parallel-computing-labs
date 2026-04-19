from locust import HttpUser, task, between

class CppServerUser(HttpUser):

    wait_time = between(0.1, 0.5)

    @task
    def load_index(self):
        self.client.get("/")

    @task
    def load_page2(self):
        self.client.get("/page2.html")
        
    @task
    def load_404(self):
        with self.client.get("/undefined", catch_response=True) as response:
            if response.status_code == 404:
                response.success()