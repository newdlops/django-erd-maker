from django.db import models


class Post(models.Model):
    class Status(models.TextChoices):
        DRAFT = "draft", "Draft"
        PUBLISHED = "published", "Published"

    author = models.ForeignKey("accounts.Author", on_delete=models.CASCADE, related_name="posts")
    status = models.CharField(choices=Status.choices, default=Status.DRAFT, max_length=20)
    title = models.CharField(max_length=200)
    tags = models.ManyToManyField("taxonomy.Tag", related_name="posts")

    @property
    def display_title(self) -> str:
        return f"{self.title} ({self.get_status_display()})"

    def publish(self):
        self.status = self.Status.PUBLISHED
        return self.author

    def tag_names(self):
        return self.tags.values_list("name", flat=True)
